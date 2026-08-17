//
// interface implemented by anything that decodes an i/o port.
//
// Spectrum port decoding is partial: a device does not compare all
// sixteen address lines, it watches the handful it cares about and
// ignores the rest. The ULA, for instance, answers every port whose
// bit 0 is low, which is half of the entire port range. So a device
// cannot be registered against a port number; it has to be asked
// whether it recognises an address, which is what handles() is for.
//
// devices are then arranged in a chain and offered each access in turn.
// that keeps the decode rules next to the device that owns them, and
// adding a Kempston interface or an AY chip later means writing one
// class and attaching it, with nothing else in the system changing.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_IO_DEVICE_H
#define SPECTRUM_IO_DEVICE_H

#include "spectrum/types.h"

namespace spectrum {

//
// one addressable peripheral on the i/o bus.
//
class io_device {
public:
    virtual ~io_device() = default;

    //
    // Returns: short identifier, reported by the diagnostics tools.
    //
    virtual const char *name() const = 0;

    //
    // Decide whether this device answers an address.
    //
    // Parameters:
    //      port        - the full 16 bit address on the bus.
    //
    // Returns:
    //      true when the device's partial decode matches.
    //
    virtual bool handles(u16 port) const = 0;

    //
    // Read from the device.
    //
    // Parameters:
    //      port          - the full 16 bit address.
    //      frame_tstate  - T-states since the frame interrupt, for
    //                      devices whose answer depends on where the
    //                      beam is.
    //
    // Returns:
    //      the byte placed on the data bus.
    //
    virtual u8 read(u16 port, u32 frame_tstate) = 0;

    //
    // Write to the device.
    //
    // Parameters:
    //      port          - the full 16 bit address.
    //      value         - the byte on the data bus.
    //      frame_tstate  - T-states since the frame interrupt.
    //
    virtual void write(u16 port, u8 value, u32 frame_tstate) = 0;
};

} // namespace spectrum

#endif // SPECTRUM_IO_DEVICE_H
