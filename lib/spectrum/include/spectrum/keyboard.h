//
// the keyboard matrix as the ULA sees it.
//
// this class is deliberately not an io_device. on real hardware the
// keyboard is not a peripheral on the bus at all: it is a passive
// matrix of switches wired straight into the ULA, and the ULA is what
// decodes port 0xFE and merges the five key bits with the tape input
// bit. modelling it the same way keeps port 0xFE owned by exactly one
// object instead of two devices fighting over the same address.
//
// state is level based, not event based. a key stays down until it is
// released, which is what the ROM keyboard scan expects: it samples the
// matrix once per frame and needs to see the same key held across
// several frames before it repeats.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_KEYBOARD_H
#define SPECTRUM_KEYBOARD_H

#include <array>
#include <vector>

#include "spectrum/keys.h"
#include "spectrum/types.h"

namespace spectrum {

//
// the 8 x 5 key matrix.
//
class keyboard {
public:
    keyboard();

    //
    // Release every key.
    //
    void reset();

    //
    // Hold a key down. pressing an already held key does nothing.
    //
    void press(key k);

    //
    // Let a key up. releasing a key that is not held does nothing.
    //
    void release(key k);

    //
    // Let every held key up.
    //
    void release_all();

    //
    // Returns: true while the key is held.
    //
    bool is_pressed(key k) const;

    //
    // Read the matrix the way the ULA does.
    //
    // Parameters:
    //      port        - the full 16 bit port address. bits 8..15
    //                    select half-rows, and a bit that is low
    //                    selects its row. several rows may be selected
    //                    at once, in which case their keys merge.
    //
    // Returns:
    //      bits 0..4 hold the five keys of the selected half-rows, low
    //      when pressed. bits 5..7 are left set for the caller to fill
    //      in with the tape input and the unused lines.
    //
    // Sample call:
    //      u8 row = kbd.scan(0xfefe);  // caps shift .. v
    //
    u8 scan(u16 port) const;

    //
    // Returns: every key currently held, in matrix order.
    //
    std::vector<key> pressed() const;

private:
    // one byte per half-row; a set bit means that key is down.
    std::array<u8, 8> rows_{};
};

} // namespace spectrum

#endif // SPECTRUM_KEYBOARD_H
