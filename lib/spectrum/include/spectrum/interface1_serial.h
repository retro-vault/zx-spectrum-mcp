//
// the RS-232 portion of Sinclair's ZX Interface 1.
//
// Interface 1 has no UART. Software controls an inverted serial data bit and
// two handshake lines through partially decoded ports $EF and $F7. This model
// turns the exact access sequence used by the Interface 1 ROM into bytes and
// connects those bytes to an optional two-socket TCP serial bridge.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_INTERFACE1_SERIAL_H
#define SPECTRUM_INTERFACE1_SERIAL_H

#include <array>
#include <cstddef>
#include <deque>
#include <span>
#include <string>

#include "spectrum/io_device.h"
#include "spectrum/serial_bridge.h"
#include "spectrum/types.h"

namespace spectrum {

//
// state exposed for diagnostics and tests.
//
struct interface1_serial_status {
    bool shadow_rom_loaded = false;
    bool shadow_rom_paged = false;
    bool bridge_running = false;
    bool data_connected = false;
    bool control_connected = false;
    bool cts_input = false;
    bool dcd_input = false;
    bool rts_output = false;
    bool dtr_output = false;
    bool interface_dtr_input = false;
    bool serial_selected = false;
    int data_port = 0;
    int control_port = 0;
    std::size_t pending_rx_bytes = 0;
    std::size_t pending_tx_bytes = 0;
    u64 rx_bytes = 0;
    u64 tx_bytes = 0;
};

//
// Interface 1 control/status and communication ports, serial subset only.
//
class interface1_serial final : public io_device {
public:
    static constexpr std::size_t shadow_rom_size = 0x2000;

    interface1_serial();
    ~interface1_serial() override;

    const char *name() const override;
    bool handles(u16 port) const override;
    u8 read(u16 port, u32 frame_tstate) override;
    void write(u16 port, u8 value, u32 frame_tstate) override;

    // Reset the hardware state and any connected bridge clients.
    void reset();

    //
    // Install an 8K Interface 1 shadow ROM.
    //
    // Returns:
    //      true when data is exactly 8192 bytes; otherwise error is set.
    //
    bool load_shadow_rom(std::span<const u8> data, std::string &error);

    // Return whether an Interface 1 shadow ROM has been installed.
    bool has_shadow_rom() const;

    // Return whether the shadow ROM currently overlays the lower 16K.
    bool shadow_rom_paged() const;

    // Page the shadow ROM when one is installed.
    void page_shadow_rom();

    // Return to the Spectrum ROM.
    void unpage_shadow_rom();

    //
    // Read the 8K shadow ROM, mirrored through the lower 16K.
    //
    u8 read_shadow_rom(u16 address) const;

    // Poll the optional host bridge as emulated time advances.
    void tick(u64 total_tstates);

    //
    // Start the data/control TCP listeners.
    //
    // Returns:
    //      true when both listeners opened; otherwise error explains why.
    //
    bool start_bridge(const serial_bridge_config &config,
                      std::string &error);

    // Stop the host bridge without changing Interface 1 port state.
    void stop_bridge();

    // Return hardware, modem, connection and queue state.
    interface1_serial_status status() const;

    //
    // Set the Interface 1 DTR input when no TCP bridge is running.
    //
    // Notes:
    //      The socket protocol's CTS input drives this pin while a bridge is
    //      active because both are the peer's permission to transmit.
    //
    void set_dtr_input(bool value);

    // Queue one host byte for the Interface 1 receive data line.
    void inject_received_byte(u8 value);

    //
    // Take one byte decoded from the Interface 1 transmit data line.
    //
    // Returns:
    //      false when no unbridged byte is waiting.
    //
    bool take_transmitted_byte(u8 &value);

private:
    enum class port_kind : u8 {
        control,
        communication,
        unknown,
    };

    static port_kind decode(u16 port);
    bool dtr_input() const;
    void read_serial_bit();
    void write_serial_bit(bool value);
    void finish_transmit(u8 value);
    void queue_received(u8 value);

    tcp_serial_bridge bridge_;
    std::array<u8, shadow_rom_size> shadow_rom_{};
    std::deque<u8> rx_fifo_;
    std::deque<u8> unbridged_tx_fifo_;

    bool manual_dtr_input_ = false;
    bool shadow_rom_loaded_ = false;
    bool shadow_rom_paged_ = false;
    bool comms_data_ = false;
    bool cts_output_ = false;
    bool input_line_ = false;

    u8 input_data_ = 0;
    int input_count_ = 0;
    u8 output_data_ = 0;
    int output_count_ = 0;

    u64 rx_bytes_ = 0;
    u64 tx_bytes_ = 0;
};

} // namespace spectrum

#endif // SPECTRUM_INTERFACE1_SERIAL_H
