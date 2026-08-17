//
// a non-blocking, two-socket TCP bridge for an emulated serial device.
//
// The data listener carries unframed bytes. The control listener carries
// newline-terminated modem commands and notifications. Keeping this host-side
// transport separate from the emulated Interface 1 makes the wire protocol
// reusable and leaves the hardware model testable without opening sockets.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_SERIAL_BRIDGE_H
#define SPECTRUM_SERIAL_BRIDGE_H

#include <cstddef>
#include <memory>
#include <string>

#include "spectrum/types.h"

namespace spectrum {

//
// configuration of one data/control TCP listener pair.
//
struct serial_bridge_config {
    int data_port = 6601;
    int control_port = 6602;
    bool require_rts = true;
    bool cts_follows_data_client = true;
};

//
// observable state of a TCP serial bridge.
//
struct serial_bridge_status {
    bool running = false;
    bool data_connected = false;
    bool control_connected = false;
    bool cts = false;
    bool dcd = false;
    int data_port = 0;
    int control_port = 0;
    std::size_t pending_rx_bytes = 0;
    std::size_t pending_tx_bytes = 0;
};

//
// raw serial bytes plus a line-oriented modem-control connection.
//
class tcp_serial_bridge {
public:
    tcp_serial_bridge();
    ~tcp_serial_bridge();

    tcp_serial_bridge(const tcp_serial_bridge &) = delete;
    tcp_serial_bridge &operator=(const tcp_serial_bridge &) = delete;

    //
    // Open the two listeners.
    //
    // Parameters:
    //      config      - ports and flow-control policy. Port zero asks the
    //                    operating system for a free port, which is useful to
    //                    tests; command-line callers reject zero.
    //      error       - receives a diagnostic when a listener cannot open.
    //
    // Returns:
    //      true when both listeners are ready.
    //
    bool start(const serial_bridge_config &config, std::string &error);

    // Close listeners and clients and discard queued bytes.
    void stop();

    //
    // Drop clients, overrides and queued bytes while keeping the listeners.
    //
    void reset();

    //
    // Poll sockets after emulated time has advanced.
    //
    // Parameters:
    //      total_tstates - T-states since machine reset.
    //      rts           - emulated request-to-send output.
    //      dtr           - emulated data-terminal-ready output.
    //
    void poll(u64 total_tstates, bool rts, bool dtr);

    //
    // Take one byte received from the data socket.
    //
    // Parameters:
    //      value       - receives the byte.
    //      rts         - current emulated request-to-send output.
    //
    // Returns:
    //      true when a byte was available and flow control allowed it.
    //
    bool receive(u8 &value, bool rts);

    // Queue one emulated byte for the data socket.
    void transmit(u8 value);

    // Return listener, connection, modem and queue state.
    serial_bridge_status status() const;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

} // namespace spectrum

#endif // SPECTRUM_SERIAL_BRIDGE_H
