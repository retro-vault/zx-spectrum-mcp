//
// implementation of the non-blocking two-socket serial bridge.
//
// The protocol deliberately matches idp-emu's TCP serial redirection:
// one raw byte stream, one line-oriented control stream, automatic modem
// inputs from the data connection, and explicit CTS/DCD overrides.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/serial_bridge.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace spectrum {

namespace {

constexpr std::size_t max_data_rx_bytes = 32768;
constexpr std::size_t max_data_tx_bytes = 32768;
constexpr std::size_t max_control_rx_bytes = 4096;
constexpr std::size_t max_control_tx_bytes = 4096;
constexpr u64 active_poll_tstates = 2048;
constexpr u64 idle_poll_tstates = 8192;

//
// Close one descriptor and mark it invalid.
//
void close_socket(int &fd)
{
    if (fd >= 0)
        ::close(fd);
    fd = -1;
}

//
// Put a socket in non-blocking mode.
//
bool set_nonblocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

//
// Open one IPv4 listener on every local interface.
//
int make_listener(int requested_port, int &bound_port,
                  std::string &error)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return -1;
    }

    int one = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(requested_port));

    if (::bind(fd, reinterpret_cast<sockaddr *>(&address),
               sizeof address) != 0) {
        error = "bind port " + std::to_string(requested_port) + ": " +
                std::strerror(errno);
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 1) != 0) {
        error = "listen on port " + std::to_string(requested_port) +
                ": " + std::strerror(errno);
        ::close(fd);
        return -1;
    }

    if (!set_nonblocking(fd)) {
        error = "make port " + std::to_string(requested_port) +
                " non-blocking: " + std::strerror(errno);
        ::close(fd);
        return -1;
    }

    sockaddr_in actual{};
    socklen_t actual_size = sizeof actual;
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&actual),
                      &actual_size) != 0) {
        error = std::string("getsockname: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }

    bound_port = ntohs(actual.sin_port);
    return fd;
}

//
// Accept one client without waiting.
//
int accept_client(int listener)
{
    sockaddr_in address{};
    socklen_t size = sizeof address;
    const int fd = ::accept(listener,
                            reinterpret_cast<sockaddr *>(&address), &size);
    if (fd < 0)
        return -1;
    if (!set_nonblocking(fd)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

//
// Parse the boolean spellings accepted by the reference bridge.
//
bool parse_boolean(std::string token, bool &value)
{
    for (char &ch : token)
        ch = static_cast<char>(std::toupper(
            static_cast<unsigned char>(ch)));

    if (token == "1" || token == "ON" || token == "TRUE" ||
        token == "YES") {
        value = true;
        return true;
    }
    if (token == "0" || token == "OFF" || token == "FALSE" ||
        token == "NO") {
        value = false;
        return true;
    }
    return false;
}

} // namespace

class tcp_serial_bridge::impl {
public:
    bool start(const serial_bridge_config &config, std::string &error)
    {
        stop();

        if (config.data_port < 0 || config.data_port > 65535 ||
            config.control_port < 0 || config.control_port > 65535) {
            error = "serial TCP ports must be between 0 and 65535";
            return false;
        }
        if (config.data_port != 0 &&
            config.data_port == config.control_port) {
            error = "serial data and control ports must differ";
            return false;
        }

        config_ = config;
        data_listener_ = make_listener(config.data_port, data_port_, error);
        if (data_listener_ < 0)
            return false;

        control_listener_ = make_listener(config.control_port,
                                           control_port_, error);
        if (control_listener_ < 0) {
            close_socket(data_listener_);
            data_port_ = 0;
            return false;
        }

        running_ = true;
        next_poll_tstate_ = 0;
        return true;
    }

    void stop()
    {
        close_socket(data_client_);
        close_socket(control_client_);
        close_socket(data_listener_);
        close_socket(control_listener_);
        running_ = false;
        data_port_ = 0;
        control_port_ = 0;
        clear_runtime();
    }

    void reset()
    {
        close_socket(data_client_);
        close_socket(control_client_);
        clear_runtime();
    }

    void poll(u64 total_tstates, bool rts, bool dtr)
    {
        if (!running_ || total_tstates < next_poll_tstate_)
            return;

        const bool active = data_client_ >= 0 || control_client_ >= 0 ||
                            !data_tx_.empty() || !control_tx_.empty();
        next_poll_tstate_ = total_tstates +
                            (active ? active_poll_tstates :
                                      idle_poll_tstates);

        accept_clients();
        receive_data();
        receive_control();
        report_modem_outputs(rts, dtr);
        flush_control();
        flush_data();
    }

    bool receive(u8 &value, bool rts)
    {
        if ((config_.require_rts && !rts) || data_rx_.empty())
            return false;

        value = data_rx_.front();
        data_rx_.pop_front();
        return true;
    }

    void transmit(u8 value)
    {
        bounded_push(data_tx_, value, max_data_tx_bytes);
    }

    serial_bridge_status status() const
    {
        serial_bridge_status result;
        result.running = running_;
        result.data_connected = data_client_ >= 0;
        result.control_connected = control_client_ >= 0;
        result.cts = effective_cts();
        result.dcd = effective_dcd();
        result.data_port = data_port_;
        result.control_port = control_port_;
        result.pending_rx_bytes = data_rx_.size();
        result.pending_tx_bytes = data_tx_.size();
        return result;
    }

private:
    template<typename value_type>
    static void bounded_push(std::deque<value_type> &queue,
                             value_type value, std::size_t limit)
    {
        if (queue.size() >= limit)
            queue.pop_front();
        queue.push_back(value);
    }

    void clear_runtime()
    {
        next_poll_tstate_ = 0;
        cts_override_active_ = false;
        cts_override_ = false;
        dcd_override_active_ = false;
        dcd_override_ = false;
        last_rts_ = false;
        last_dtr_ = false;
        control_rx_.clear();
        control_tx_.clear();
        data_rx_.clear();
        data_tx_.clear();
    }

    void accept_clients()
    {
        if (data_client_ < 0)
            data_client_ = accept_client(data_listener_);
        if (control_client_ < 0)
            control_client_ = accept_client(control_listener_);
    }

    void receive_data()
    {
        if (data_client_ < 0)
            return;

        std::array<u8, 512> buffer{};
        for (;;) {
            const ssize_t count = ::recv(data_client_, buffer.data(),
                                         buffer.size(), 0);
            if (count > 0) {
                for (ssize_t i = 0; i < count; ++i) {
                    bounded_push(data_rx_, buffer[static_cast<std::size_t>(i)],
                                 max_data_rx_bytes);
                }
                continue;
            }
            if (count == 0 ||
                (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close_socket(data_client_);
            }
            break;
        }
    }

    void receive_control()
    {
        if (control_client_ < 0)
            return;

        std::array<char, 256> buffer{};
        for (;;) {
            const ssize_t count = ::recv(control_client_, buffer.data(),
                                         buffer.size(), 0);
            if (count > 0) {
                control_rx_.append(buffer.data(),
                                   static_cast<std::size_t>(count));
                if (control_rx_.size() > max_control_rx_bytes) {
                    control_rx_.erase(0, control_rx_.size() -
                                             max_control_rx_bytes);
                }
                parse_control_lines();
                continue;
            }
            if (count == 0 ||
                (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close_socket(control_client_);
                control_rx_.clear();
                control_tx_.clear();
            }
            break;
        }
    }

    void parse_control_lines()
    {
        for (;;) {
            const std::size_t newline = control_rx_.find('\n');
            if (newline == std::string::npos)
                return;

            std::string line = control_rx_.substr(0, newline);
            control_rx_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            parse_control_line(line);
        }
    }

    void parse_control_line(const std::string &line)
    {
        std::string command;
        std::string argument;
        const std::size_t space = line.find_first_of(" \t");
        if (space == std::string::npos) {
            command = line;
        } else {
            command = line.substr(0, space);
            const std::size_t start = line.find_first_not_of(" \t", space);
            if (start != std::string::npos)
                argument = line.substr(start);
        }

        for (char &ch : command) {
            ch = static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch)));
        }
        for (char &ch : argument) {
            ch = static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch)));
        }

        if (command == "PING") {
            queue_control("PONG\n");
        } else if (command == "CTS") {
            parse_override(argument, cts_override_active_, cts_override_,
                           "CTS");
        } else if (command == "DCD") {
            parse_override(argument, dcd_override_active_, dcd_override_,
                           "DCD");
        } else {
            queue_control("ERR UNKNOWN\n");
        }
    }

    void parse_override(const std::string &argument, bool &active,
                        bool &value, const char *name)
    {
        if (argument == "AUTO") {
            active = false;
            queue_control(std::string("OK ") + name + " AUTO\n");
            return;
        }

        bool parsed = false;
        if (parse_boolean(argument, parsed)) {
            active = true;
            value = parsed;
            queue_control(std::string("OK ") + name + "\n");
            return;
        }

        queue_control(std::string("ERR ") + name + " <0|1|AUTO>\n");
    }

    void report_modem_outputs(bool rts, bool dtr)
    {
        if (last_rts_ != rts) {
            last_rts_ = rts;
            queue_modem_state("RTS", rts);
        }
        if (last_dtr_ != dtr) {
            last_dtr_ = dtr;
            queue_modem_state("DTR", dtr);
        }
    }

    void queue_modem_state(const char *name, bool value)
    {
        if (control_client_ < 0)
            return;

        char line[32];
        std::snprintf(line, sizeof line, "%s %d\n", name,
                      value ? 1 : 0);
        queue_control(line);
    }

    void queue_control(const std::string &text)
    {
        if (control_client_ < 0)
            return;
        for (char ch : text)
            bounded_push(control_tx_, ch, max_control_tx_bytes);
    }

    void flush_control()
    {
        if (control_client_ < 0)
            return;

        while (!control_tx_.empty()) {
            std::array<char, 512> buffer{};
            const std::size_t size = std::min(buffer.size(),
                                              control_tx_.size());
            std::copy_n(control_tx_.begin(), size, buffer.begin());

            const ssize_t sent = ::send(control_client_, buffer.data(), size,
                                        MSG_NOSIGNAL);
            if (sent > 0) {
                for (ssize_t i = 0; i < sent; ++i)
                    control_tx_.pop_front();
                continue;
            }
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;
            close_socket(control_client_);
            control_rx_.clear();
            control_tx_.clear();
            return;
        }
    }

    void flush_data()
    {
        if (data_client_ < 0)
            return;

        while (!data_tx_.empty()) {
            std::array<u8, 1024> buffer{};
            const std::size_t size = std::min(buffer.size(), data_tx_.size());
            std::copy_n(data_tx_.begin(), size, buffer.begin());

            const ssize_t sent = ::send(data_client_, buffer.data(), size,
                                        MSG_NOSIGNAL);
            if (sent > 0) {
                for (ssize_t i = 0; i < sent; ++i)
                    data_tx_.pop_front();
                continue;
            }
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;
            close_socket(data_client_);
            return;
        }
    }

    bool effective_cts() const
    {
        bool value = config_.cts_follows_data_client
                         ? data_client_ >= 0
                         : true;
        if (cts_override_active_)
            value = cts_override_;
        return value;
    }

    bool effective_dcd() const
    {
        bool value = data_client_ >= 0;
        if (dcd_override_active_)
            value = dcd_override_;
        return value;
    }

    serial_bridge_config config_{};
    bool running_ = false;
    int data_listener_ = -1;
    int control_listener_ = -1;
    int data_client_ = -1;
    int control_client_ = -1;
    int data_port_ = 0;
    int control_port_ = 0;
    u64 next_poll_tstate_ = 0;

    bool cts_override_active_ = false;
    bool cts_override_ = false;
    bool dcd_override_active_ = false;
    bool dcd_override_ = false;
    bool last_rts_ = false;
    bool last_dtr_ = false;

    std::string control_rx_;
    std::deque<char> control_tx_;
    std::deque<u8> data_rx_;
    std::deque<u8> data_tx_;
};

tcp_serial_bridge::tcp_serial_bridge() : impl_(std::make_unique<impl>())
{
}

tcp_serial_bridge::~tcp_serial_bridge() = default;

bool tcp_serial_bridge::start(const serial_bridge_config &config,
                              std::string &error)
{
    return impl_->start(config, error);
}

void tcp_serial_bridge::stop() { impl_->stop(); }

void tcp_serial_bridge::reset() { impl_->reset(); }

void tcp_serial_bridge::poll(u64 total_tstates, bool rts, bool dtr)
{
    impl_->poll(total_tstates, rts, dtr);
}

bool tcp_serial_bridge::receive(u8 &value, bool rts)
{
    return impl_->receive(value, rts);
}

void tcp_serial_bridge::transmit(u8 value) { impl_->transmit(value); }

serial_bridge_status tcp_serial_bridge::status() const
{
    return impl_->status();
}

} // namespace spectrum
