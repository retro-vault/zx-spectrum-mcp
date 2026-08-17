//
// implementation of the ZX Interface 1 RS-232 hardware subset.
//
// The byte codec follows the access sequences in Sinclair's Interface 1 ROM:
// input detects four samples of the inverted start bit and output recognises
// the ROM's idle, start, eight data, two stop and trailing-idle writes. This
// makes every supported ROM baud rate work without coupling host I/O to wall
// clock time.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/interface1_serial.h"

#include <algorithm>

namespace spectrum {

namespace {

constexpr std::size_t max_interface_rx_bytes = 8192;
constexpr std::size_t max_unbridged_tx_bytes = 32768;

} // namespace

interface1_serial::interface1_serial() { reset(); }

interface1_serial::~interface1_serial() = default;

const char *interface1_serial::name() const
{
    return "ZX Interface 1 RS-232";
}

interface1_serial::port_kind interface1_serial::decode(u16 port)
{
    switch (port & 0x0018) {
    case 0x0008:
        return port_kind::control;
    case 0x0010:
        return port_kind::communication;
    default:
        return port_kind::unknown;
    }
}

bool interface1_serial::handles(u16 port) const
{
    return decode(port) != port_kind::unknown;
}

u8 interface1_serial::read(u16 port, u32)
{
    switch (decode(port)) {
    case port_kind::control: {
        // GAP, SYNC and write protect float high in this serial-only model.
        // BUSY is inactive-low in Fuse's Interface 1 model.
        u8 value = 0xef;
        if (!dtr_input())
            value &= 0xf7;
        return value;
    }
    case port_kind::communication:
        read_serial_bit();
        // Bit 7 is the inverted serial input. The unused Sinclair Network
        // input rests low, so bit 0 reads as zero.
        return input_line_ ? 0xfe : 0x7e;
    case port_kind::unknown:
        return 0xff;
    }
    return 0xff;
}

void interface1_serial::write(u16 port, u8 value, u32)
{
    switch (decode(port)) {
    case port_kind::control: {
        const bool selected = (value & 0x01) != 0;
        if (selected && !comms_data_) {
            input_count_ = 0;
            input_data_ = 0;
            output_count_ = 0;
            output_data_ = 0;
        }
        comms_data_ = selected;
        cts_output_ = (value & 0x10) != 0;
        break;
    }
    case port_kind::communication:
        if (comms_data_)
            write_serial_bit((value & 0x01) != 0);
        break;
    case port_kind::unknown:
        break;
    }
}

void interface1_serial::reset()
{
    bridge_.reset();
    rx_fifo_.clear();
    unbridged_tx_fifo_.clear();
    manual_dtr_input_ = false;
    shadow_rom_paged_ = false;
    comms_data_ = false;
    cts_output_ = false;
    input_line_ = false;
    input_data_ = 0;
    input_count_ = 0;
    output_data_ = 0;
    output_count_ = 0;
    rx_bytes_ = 0;
    tx_bytes_ = 0;
}

bool interface1_serial::load_shadow_rom(std::span<const u8> data,
                                        std::string &error)
{
    if (data.size() != shadow_rom_size) {
        error = "Interface 1 ROM must be exactly 8192 bytes";
        return false;
    }

    std::copy(data.begin(), data.end(), shadow_rom_.begin());
    shadow_rom_loaded_ = true;
    shadow_rom_paged_ = false;
    error.clear();
    return true;
}

bool interface1_serial::has_shadow_rom() const
{
    return shadow_rom_loaded_;
}

bool interface1_serial::shadow_rom_paged() const
{
    return shadow_rom_paged_;
}

void interface1_serial::page_shadow_rom()
{
    if (shadow_rom_loaded_)
        shadow_rom_paged_ = true;
}

void interface1_serial::unpage_shadow_rom()
{
    shadow_rom_paged_ = false;
}

u8 interface1_serial::read_shadow_rom(u16 address) const
{
    return shadow_rom_[address & 0x1fff];
}

void interface1_serial::tick(u64 total_tstates)
{
    bridge_.poll(total_tstates, cts_output_, comms_data_);

    u8 value = 0;
    while (bridge_.receive(value, cts_output_))
        queue_received(value);
}

bool interface1_serial::start_bridge(const serial_bridge_config &config,
                                     std::string &error)
{
    return bridge_.start(config, error);
}

void interface1_serial::stop_bridge() { bridge_.stop(); }

interface1_serial_status interface1_serial::status() const
{
    const serial_bridge_status bridge = bridge_.status();

    interface1_serial_status result;
    result.shadow_rom_loaded = shadow_rom_loaded_;
    result.shadow_rom_paged = shadow_rom_paged_;
    result.bridge_running = bridge.running;
    result.data_connected = bridge.data_connected;
    result.control_connected = bridge.control_connected;
    result.cts_input = bridge.cts;
    result.dcd_input = bridge.dcd;
    result.rts_output = cts_output_;
    result.dtr_output = comms_data_;
    result.interface_dtr_input = dtr_input();
    result.serial_selected = comms_data_;
    result.data_port = bridge.data_port;
    result.control_port = bridge.control_port;
    result.pending_rx_bytes = rx_fifo_.size() + bridge.pending_rx_bytes;
    result.pending_tx_bytes = unbridged_tx_fifo_.size() +
                              bridge.pending_tx_bytes;
    result.rx_bytes = rx_bytes_;
    result.tx_bytes = tx_bytes_;
    return result;
}

void interface1_serial::set_dtr_input(bool value)
{
    manual_dtr_input_ = value;
}

void interface1_serial::inject_received_byte(u8 value)
{
    queue_received(value);
}

bool interface1_serial::take_transmitted_byte(u8 &value)
{
    if (unbridged_tx_fifo_.empty())
        return false;
    value = unbridged_tx_fifo_.front();
    unbridged_tx_fifo_.pop_front();
    return true;
}

bool interface1_serial::dtr_input() const
{
    const serial_bridge_status bridge = bridge_.status();
    return bridge.running ? bridge.cts : manual_dtr_input_;
}

void interface1_serial::read_serial_bit()
{
    if (!cts_output_) {
        input_count_ = 0;
        input_line_ = false;
        return;
    }

    if (input_count_ == 0) {
        if (!rx_fifo_.empty()) {
            input_data_ = rx_fifo_.front();
            rx_fifo_.pop_front();
            ++input_count_;
        }
        input_line_ = false;
    } else if (input_count_ < 5) {
        input_line_ = true;
        ++input_count_;
    } else if (input_count_ < 13) {
        input_line_ = (input_data_ & 0x01) == 0;
        input_data_ >>= 1;
        ++input_count_;
    } else {
        input_count_ = 0;
    }
}

void interface1_serial::write_serial_bit(bool value)
{
    bool malformed = false;

    if (output_count_ == 0 && !value) {
        ++output_count_;
    } else if (output_count_ == 1) {
        if (cts_output_ || !value)
            malformed = true;
        else
            ++output_count_;
    } else if (output_count_ >= 2 && output_count_ <= 9) {
        output_data_ >>= 1;
        if (!value)
            output_data_ |= 0x80;
        ++output_count_;
    } else if (output_count_ >= 10 && output_count_ <= 11) {
        if (value)
            malformed = true;
        else
            ++output_count_;
    } else if (output_count_ == 12) {
        if (!value)
            malformed = true;
        else
            ++output_count_;
    } else if (output_count_ == 13 && value) {
        malformed = true;
    }

    if (malformed) {
        output_count_ = 13;
        output_data_ = '?';
    }

    if (output_count_ == 13) {
        finish_transmit(output_data_);
        output_count_ = 0;
        output_data_ = 0;
    }
}

void interface1_serial::finish_transmit(u8 value)
{
    ++tx_bytes_;
    if (bridge_.status().running) {
        bridge_.transmit(value);
        return;
    }

    if (unbridged_tx_fifo_.size() >= max_unbridged_tx_bytes)
        unbridged_tx_fifo_.pop_front();
    unbridged_tx_fifo_.push_back(value);
}

void interface1_serial::queue_received(u8 value)
{
    if (rx_fifo_.size() >= max_interface_rx_bytes)
        rx_fifo_.pop_front();
    rx_fifo_.push_back(value);
    ++rx_bytes_;
}

} // namespace spectrum
