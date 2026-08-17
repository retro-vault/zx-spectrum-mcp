//
// ZX Interface 1 serial framing and its two-port TCP bridge.
//
// These are ordinary C++ tests like every other suite in this project. They
// exercise the real mirrored hardware ports, run an OUT program on the Z80,
// and connect loopback clients to both TCP listeners to pin the data/control
// protocol copied from idp-emu.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "spectrum/interface1_serial.h"
#include "spectrum/machine.h"
#include "test_support.h"

using namespace spectrum;

namespace {

constexpr u16 control_port = 0x00ef;
constexpr u16 communication_port = 0x00f7;

//
// Write the exact inverted frame emitted by the Interface 1 ROM.
//
void write_rom_frame(interface1_serial &serial, u8 value)
{
    serial.write(communication_port, 0, 0); // resting level
    serial.write(communication_port, 1, 0); // start bit
    for (int bit = 0; bit < 8; ++bit) {
        const bool inverted = (value & (1u << bit)) == 0;
        serial.write(communication_port, inverted ? 1 : 0, 0);
    }
    serial.write(communication_port, 0, 0); // stop bit 1
    serial.write(communication_port, 0, 0); // stop bit 2
    serial.write(communication_port, 1, 0); // trailing idle
}

//
// Read one byte using the sampling sequence in the Interface 1 ROM.
//
u8 read_rom_frame(interface1_serial &serial)
{
    // The first low sample arms the byte, then the ROM confirms four high
    // samples of the inverted start bit.
    (void)serial.read(communication_port, 0);
    for (int sample = 0; sample < 4; ++sample)
        (void)serial.read(communication_port, 0);

    u8 value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        const u8 sample = serial.read(communication_port, 0);
        if ((sample & 0x80) == 0)
            value |= static_cast<u8>(1u << bit);
    }

    // Finish this emulated frame so the next call begins at count zero.
    (void)serial.read(communication_port, 0);
    return value;
}

void test_interface1_ports()
{
    test::section("Interface 1 port decoding and handshake");

    interface1_serial serial;
    test::check(serial.handles(control_port), "$EF is the control port");
    test::check(serial.handles(communication_port),
                "$F7 is the communication port");
    test::check(serial.handles(0x12ef), "$EF is partially decoded");
    test::check(serial.handles(0xabf7), "$F7 is partially decoded");
    test::check(!serial.handles(0x00e7),
                "$E7 microdrive data is outside the serial subset");

    test::check_eq(serial.read(control_port, 0) & 0x08, 0,
                   "DTR input is low without a peer");
    serial.set_dtr_input(true);
    test::check_eq(serial.read(control_port, 0) & 0x08, 0x08,
                   "DTR input appears on control bit 3");

    serial.write(control_port, 0x11, 0);
    const interface1_serial_status state = serial.status();
    test::check(state.serial_selected,
                "control bit 0 selects RS-232 output");
    test::check(state.rts_output,
                "Interface 1 CTS is exported as bridge RTS");
}

void test_interface1_byte_codec()
{
    test::section("Interface 1 ROM byte framing");

    interface1_serial serial;
    serial.set_dtr_input(true);

    // The output routine selects serial with Interface 1 CTS low.
    serial.write(control_port, 0x01, 0);
    write_rom_frame(serial, 0x00);
    write_rom_frame(serial, 0xa5);
    write_rom_frame(serial, 0xff);

    u8 value = 0;
    test::check(serial.take_transmitted_byte(value),
                "the first transmitted frame produces a byte");
    test::check_eq(value, 0x00, "zero survives inverted framing");
    test::check(serial.take_transmitted_byte(value),
                "the second transmitted frame produces a byte");
    test::check_eq(value, 0xa5, "mixed bits survive inverted framing");
    test::check(serial.take_transmitted_byte(value),
                "the third transmitted frame produces a byte");
    test::check_eq(value, 0xff, "ones survive inverted framing");
    test::check(!serial.take_transmitted_byte(value),
                "the transmit queue is drained");

    // CTS high means the Spectrum is ready to receive.
    serial.write(control_port, 0x11, 0);
    serial.inject_received_byte(0x00);
    serial.inject_received_byte(0x5a);
    serial.inject_received_byte(0xff);
    test::check_eq(read_rom_frame(serial), 0x00,
                   "the ROM sampling sequence receives zero");
    test::check_eq(read_rom_frame(serial), 0x5a,
                   "the ROM sampling sequence receives mixed bits");
    test::check_eq(read_rom_frame(serial), 0xff,
                   "the ROM sampling sequence receives ones");

    serial.write(control_port, 0x01, 0);
    serial.inject_received_byte(0x42);
    (void)serial.read(communication_port, 0);
    test::check_eq(serial.status().pending_rx_bytes, 1,
                   "remote input waits while Interface 1 CTS is low");
}

void test_z80_reaches_interface1()
{
    test::section("Z80 I/O reaches Interface 1");

    machine target;
    target.enable_interface1_serial();

    std::vector<u8> program;
    const auto out = [&program](u8 port, u8 value) {
        program.push_back(0x3e); // LD A,n
        program.push_back(value);
        program.push_back(0xd3); // OUT (n),A
        program.push_back(port);
    };

    out(0xef, 0x01);
    out(0xf7, 0x00);
    out(0xf7, 0x01);
    const u8 transmitted = 0x96;
    for (int bit = 0; bit < 8; ++bit)
        out(0xf7, (transmitted & (1u << bit)) == 0 ? 1 : 0);
    out(0xf7, 0x00);
    out(0xf7, 0x00);
    out(0xf7, 0x01);
    program.push_back(0x76); // HALT

    target.mem().write_block(0x8000, program, false);
    target.processor().jump(0x8000);
    run_limits limits;
    limits.max_tstates = 10000;
    limits.stop_on_halt = true;
    const run_result result = target.run(limits);

    u8 decoded = 0;
    test::check_eq(static_cast<int>(result.reason),
                   static_cast<int>(stop_reason::halted),
                   "the serial writer program halts");
    test::check(target.serial().take_transmitted_byte(decoded),
                "the hardware decoded the Z80's OUT sequence");
    test::check_eq(decoded, transmitted,
                   "the decoded byte is the program's byte");
}

void test_interface1_shadow_rom()
{
    test::section("Interface 1 shadow ROM paging");

    machine target;
    std::vector<u8> spectrum_rom(memory::rom_size, 0x31);
    target.mem().load_rom(spectrum_rom);

    std::vector<u8> bad_rom(interface1_serial::shadow_rom_size - 1, 0);
    std::string error;
    test::check(!target.load_interface1_rom(bad_rom, error),
                "an incorrectly sized shadow ROM is refused");
    test::check_contains(error, "8192",
                         "the size error says what Interface 1 needs");
    test::check(!target.interface1_serial_enabled(),
                "a bad image does not attach the interface");

    std::vector<u8> shadow(interface1_serial::shadow_rom_size);
    for (std::size_t i = 0; i < shadow.size(); ++i)
        shadow[i] = static_cast<u8>((i * 37 + 11) & 0xff);

    const bool loaded = target.load_interface1_rom(shadow, error);
    test::check(loaded, "an 8K shadow ROM loads: " + error);
    test::check(target.interface1_serial_enabled(),
                "loading the ROM attaches Interface 1");
    test::check(target.serial().status().shadow_rom_loaded,
                "status reports the installed shadow ROM");
    test::check_eq(target.memory_read(0x0008), 0x31,
                   "the Spectrum ROM is initially visible");

    test::check_eq(target.opcode_fetch(0x0008), shadow[0x0008],
                   "an opcode fetch at $0008 pages Interface 1 first");
    test::check(target.serial().shadow_rom_paged(),
                "the shadow ROM remains paged after the trap");
    test::check_eq(target.memory_read(0x2008), shadow[0x0008],
                   "the 8K shadow image mirrors through the lower 16K");

    test::check_eq(target.opcode_fetch(0x0700), shadow[0x0700],
                   "$0700 itself is fetched from the shadow ROM");
    test::check(!target.serial().shadow_rom_paged(),
                "$0700 unpages Interface 1 after its opcode fetch");
    test::check_eq(target.memory_read(0x0700), 0x31,
                   "the Spectrum ROM is visible again afterwards");

    (void)target.opcode_fetch(0x1708);
    test::check(target.serial().shadow_rom_paged(),
                "$1708 is the second hardware paging trap");
    target.reset();
    test::check(target.serial().has_shadow_rom(),
                "reset preserves the installed Interface 1 ROM");
    test::check(!target.serial().shadow_rom_paged(),
                "reset releases shadow ROM paging");
}

//
// A small owning loopback client used by the live bridge checks.
//
class socket_client {
public:
    ~socket_client()
    {
        if (fd_ >= 0)
            ::close(fd_);
    }

    bool connect_to(int port)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
            return false;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr *>(&address),
                      sizeof address) != 0) {
            return false;
        }

        const int flags = ::fcntl(fd_, F_GETFL, 0);
        return flags >= 0 &&
               ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool send_text(const std::string &text)
    {
        return send_bytes(reinterpret_cast<const u8 *>(text.data()),
                          text.size());
    }

    bool send_bytes(const u8 *data, std::size_t size)
    {
        std::size_t offset = 0;
        while (offset < size) {
            const ssize_t sent = ::send(fd_, data + offset, size - offset,
                                        MSG_NOSIGNAL);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            return false;
        }
        return true;
    }

    std::string receive_available()
    {
        std::string result;
        pollfd ready{};
        ready.fd = fd_;
        ready.events = POLLIN;
        if (::poll(&ready, 1, 100) <= 0)
            return result;

        char buffer[512];
        for (;;) {
            const ssize_t count = ::recv(fd_, buffer, sizeof buffer, 0);
            if (count > 0) {
                result.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            break;
        }
        return result;
    }

private:
    int fd_ = -1;
};

void pump(interface1_serial &serial, u64 &tstates)
{
    tstates += 9000;
    serial.tick(tstates);
}

void test_two_port_tcp_bridge()
{
    test::section("two-port TCP serial protocol");

    interface1_serial serial;
    serial_bridge_config config;
    config.data_port = 0;
    config.control_port = 0;

    std::string error;
    test::check(serial.start_bridge(config, error),
                "ephemeral data and control listeners open: " + error);
    const interface1_serial_status listening = serial.status();
    test::check(listening.data_port > 0,
                "the data listener has a real port");
    test::check(listening.control_port > 0,
                "the control listener has a real port");
    test::check(listening.data_port != listening.control_port,
                "data and control use different ports");

    socket_client data;
    socket_client control;
    test::check(data.connect_to(listening.data_port),
                "a data client connects");
    test::check(control.connect_to(listening.control_port),
                "a control client connects");

    u64 tstates = 0;
    pump(serial, tstates);
    test::check(serial.status().data_connected,
                "the bridge accepts the data client");
    test::check(serial.status().control_connected,
                "the bridge accepts the control client");
    test::check(serial.status().interface_dtr_input,
                "a data client asserts the Interface 1 DTR input");

    test::check(control.send_text("ping\r\nCTS off\nDCD 0\nWHAT\n"),
                "control commands are sent");
    pump(serial, tstates);
    const std::string replies = control.receive_available();
    test::check_contains(replies, "PONG\n", "PING is answered");
    test::check_contains(replies, "OK CTS\n",
                         "CTS accepts case-insensitive booleans");
    test::check_contains(replies, "OK DCD\n", "DCD can be overridden");
    test::check_contains(replies, "ERR UNKNOWN\n",
                         "unknown commands are rejected");
    test::check(!serial.status().interface_dtr_input,
                "CTS 0 lowers the Interface 1 DTR input");
    test::check(!serial.status().dcd_input, "DCD 0 lowers carrier detect");

    test::check(control.send_text("CTS AUTO\nDCD AUTO\n"),
                "automatic modem inputs are restored");
    pump(serial, tstates);
    const std::string automatic = control.receive_available();
    test::check_contains(automatic, "OK CTS AUTO\n",
                         "CTS AUTO is acknowledged");
    test::check_contains(automatic, "OK DCD AUTO\n",
                         "DCD AUTO is acknowledged");
    test::check(serial.status().interface_dtr_input,
                "automatic CTS follows the data client");
    test::check(serial.status().dcd_input,
                "automatic DCD follows the data client");

    serial.write(control_port, 0x11, 0);
    test::check(serial.status().rts_output,
                "control bit 4 raises the published RTS output");
    test::check(serial.status().dtr_output,
                "control bit 0 raises the published DTR output");
    pump(serial, tstates);
    pump(serial, tstates);
    const std::string modem = control.receive_available();
    test::check_contains(modem, "RTS 1\n",
                         "Interface 1 CTS publishes bridge RTS");
    test::check_contains(modem, "DTR 1\n",
                         "serial selection publishes bridge DTR");

    const u8 incoming[] = {0x00, 0x5a, 0xff};
    test::check(data.send_bytes(incoming, sizeof incoming),
                "raw bytes are sent to the data socket");
    pump(serial, tstates);
    test::check_eq(read_rom_frame(serial), 0x00,
                   "socket zero reaches the serial input");
    test::check_eq(read_rom_frame(serial), 0x5a,
                   "socket mixed byte reaches the serial input");
    test::check_eq(read_rom_frame(serial), 0xff,
                   "socket ones reach the serial input");

    serial.write(control_port, 0x01, 0);
    write_rom_frame(serial, 0xc3);
    pump(serial, tstates);
    const std::string outgoing = data.receive_available();
    test::check_eq(outgoing.size(), 1,
                   "one emulated byte reaches the data socket");
    if (!outgoing.empty()) {
        test::check_eq(static_cast<unsigned char>(outgoing[0]), 0xc3,
                       "the data socket carries the decoded byte raw");
    }
}

void test_bridge_configuration_errors()
{
    test::section("serial bridge configuration");

    tcp_serial_bridge bridge;
    serial_bridge_config same;
    same.data_port = 12345;
    same.control_port = 12345;
    std::string error;
    test::check(!bridge.start(same, error),
                "the same data and control port is refused");
    test::check_contains(error, "must differ",
                         "the configuration error is specific");
}

} // namespace

int main()
{
    test_interface1_ports();
    test_interface1_byte_codec();
    test_z80_reaches_interface1();
    test_interface1_shadow_rom();
    test_two_port_tcp_bridge();
    test_bridge_configuration_errors();
    return test::summary("serial");
}
