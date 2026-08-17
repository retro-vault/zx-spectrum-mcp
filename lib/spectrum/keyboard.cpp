//
// implementation of the keyboard matrix.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/keyboard.h"

namespace spectrum {

keyboard::keyboard() { reset(); }

void keyboard::reset() { rows_.fill(0); }

void keyboard::press(key k)
{
    rows_[static_cast<std::size_t>(key_row(k))] |=
        static_cast<u8>(1u << key_bit(k));
}

void keyboard::release(key k)
{
    rows_[static_cast<std::size_t>(key_row(k))] &=
        static_cast<u8>(~(1u << key_bit(k)));
}

void keyboard::release_all() { rows_.fill(0); }

bool keyboard::is_pressed(key k) const
{
    const u8 row = rows_[static_cast<std::size_t>(key_row(k))];
    return (row & (1u << key_bit(k))) != 0;
}

u8 keyboard::scan(u16 port) const
{
    // a key that is down pulls its bit low, so start from all released
    // and clear a bit for every pressed key in a selected half-row.
    u8 result = 0x1f;

    const auto selector = static_cast<u8>(port >> 8);
    for (int row = 0; row < 8; ++row) {
        if ((selector & (1u << row)) != 0)
            continue;
        result &= static_cast<u8>(~rows_[static_cast<std::size_t>(row)]);
    }

    return static_cast<u8>(result & 0x1f);
}

std::vector<key> keyboard::pressed() const
{
    std::vector<key> out;
    for (const key k : all_keys()) {
        if (is_pressed(k))
            out.push_back(k);
    }
    return out;
}

} // namespace spectrum
