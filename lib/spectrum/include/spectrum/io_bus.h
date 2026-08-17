//
// the i/o bus: a chain of responsibility over the attached devices.
//
// an access walks the chain and stops at the first device whose partial
// decode claims the address. attachment order is therefore priority
// order, exactly as edge connector hardware behaves when two devices
// answer overlapping addresses.
//
// a read that no device claims does not return a defined value on real
// hardware. on a 48K it returns whatever the ULA happened to be putting
// on the bus, the so-called floating bus, which some games use to sync
// to the beam. the fallback hook exists so the ULA can supply that
// without the bus needing to know anything about video timing.
//
// devices are referenced, not owned. the machine owns them, and it
// outlives the bus.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_IO_BUS_H
#define SPECTRUM_IO_BUS_H

#include <functional>
#include <vector>

#include "spectrum/io_device.h"
#include "spectrum/types.h"

namespace spectrum {

//
// routes port accesses to the device that decodes them.
//
class io_bus {
public:
    //
    // supplies the byte for a read no device claimed.
    //
    using fallback_reader = std::function<u8(u16 port, u32 frame_tstate)>;

    //
    // Add a device to the end of the chain.
    //
    // Parameters:
    //      device      - must outlive the bus.
    //
    // Notes:
    //      earlier devices win when two of them decode the same
    //      address.
    //
    void attach(io_device &device);

    //
    // Remove every device from the chain.
    //
    void detach_all();

    //
    // Read a port.
    //
    // Returns:
    //      the claiming device's answer, or the fallback reader's
    //      answer, or 0xFF when no fallback is installed.
    //
    u8 read(u16 port, u32 frame_tstate);

    //
    // Write a port. ignored when no device claims the address.
    //
    void write(u16 port, u8 value, u32 frame_tstate);

    //
    // Install the handler for unclaimed reads.
    //
    void set_fallback_reader(fallback_reader reader);

    //
    // Find which device would answer an address, without performing
    // an access. used by the diagnostics tools.
    //
    // Returns:
    //      the device, or nullptr when the address is unclaimed.
    //
    const io_device *device_for(u16 port) const;

    //
    // Returns: every attached device, in chain order.
    //
    std::vector<const io_device *> devices() const;

private:
    std::vector<io_device *> devices_;
    fallback_reader fallback_;
};

} // namespace spectrum

#endif // SPECTRUM_IO_BUS_H
