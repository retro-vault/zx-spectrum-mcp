//
// implementation of the i/o bus chain.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/io_bus.h"

#include <algorithm>

namespace spectrum {

void io_bus::attach(io_device &device) { devices_.push_back(&device); }

void io_bus::detach_all() { devices_.clear(); }

u8 io_bus::read(u16 port, u32 frame_tstate)
{
    for (io_device *device : devices_) {
        if (device->handles(port))
            return device->read(port, frame_tstate);
    }

    if (fallback_)
        return fallback_(port, frame_tstate);

    // nothing driving the bus and nobody to ask.
    return 0xff;
}

void io_bus::write(u16 port, u8 value, u32 frame_tstate)
{
    for (io_device *device : devices_) {
        if (device->handles(port)) {
            device->write(port, value, frame_tstate);
            return;
        }
    }
}

void io_bus::set_fallback_reader(fallback_reader reader)
{
    fallback_ = std::move(reader);
}

const io_device *io_bus::device_for(u16 port) const
{
    const auto it = std::find_if(
        devices_.begin(), devices_.end(),
        [port](const io_device *d) { return d->handles(port); });

    return it != devices_.end() ? *it : nullptr;
}

std::vector<const io_device *> io_bus::devices() const
{
    return std::vector<const io_device *>(devices_.begin(),
                                          devices_.end());
}

} // namespace spectrum
