//
// the 40 key Spectrum matrix, its names, and character translation.
//
// the matrix is eight half-rows of five keys. each half-row is selected
// by pulling one of the address lines A8..A15 low while reading port
// 0xFE, and the five keys of that half-row appear in bits 0..4, low
// when pressed. the key enum encodes that layout directly as
// (row << 3) | bit, so scanning is arithmetic rather than a lookup.
//
// character translation exists so a caller can ask for the text "PRINT"
// instead of working out that a quote mark is symbol shift plus P.
// every printable character the machine can enter from the keyboard has
// an entry, including the symbol shifted punctuation.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_KEYS_H
#define SPECTRUM_KEYS_H

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "spectrum/types.h"

namespace spectrum {

//
// one key of the matrix, encoded as (half-row << 3) | bit.
//
enum class key : u8 {
    // half-row 0, selected by A8 low, port 0xFEFE
    caps_shift = 0x00,
    z,
    x,
    c,
    v,

    // half-row 1, A9 low, port 0xFDFE
    a = 0x08,
    s,
    d,
    f,
    g,

    // half-row 2, A10 low, port 0xFBFE
    q = 0x10,
    w,
    e,
    r,
    t,

    // half-row 3, A11 low, port 0xF7FE
    num_1 = 0x18,
    num_2,
    num_3,
    num_4,
    num_5,

    // half-row 4, A12 low, port 0xEFFE
    num_0 = 0x20,
    num_9,
    num_8,
    num_7,
    num_6,

    // half-row 5, A13 low, port 0xDFFE
    p = 0x28,
    o,
    i,
    u,
    y,

    // half-row 6, A14 low, port 0xBFFE
    enter = 0x30,
    l,
    k,
    j,
    h,

    // half-row 7, A15 low, port 0x7FFE
    space = 0x38,
    symbol_shift,
    m,
    n,
    b,
};

//
// Returns: which half-row, 0..7, the key lives in.
//
constexpr int key_row(key k) { return (static_cast<int>(k) >> 3) & 7; }

//
// Returns: which data bit, 0..4, the key drives.
//
constexpr int key_bit(key k) { return static_cast<int>(k) & 7; }

//
// a key plus the shift key that has to be held with it.
//
struct key_chord {
    key primary;
    std::optional<key> modifier;
};

//
// Returns: the canonical name of a key, such as "A" or "CAPS_SHIFT".
//
const char *key_name(key k);

//
// Look up a key by name.
//
// Parameters:
//      name        - a canonical name, or a common alias. matching
//                    ignores case, spaces, hyphens and underscores, so
//                    "caps shift", "CAPS_SHIFT" and "capsshift" all
//                    work. "CS" and "SS" are accepted for the two
//                    shift keys.
//
// Returns:
//      the key, or nullopt when the name is not recognised.
//
std::optional<key> key_from_name(std::string_view name);

//
// Returns: every key of the matrix, in row then bit order.
//
std::span<const key> all_keys();

//
// Work out which keys enter a character.
//
// Parameters:
//      c           - the character to type.
//
// Returns:
//      the key and any required shift, or nullopt when the character
//      cannot be typed on a Spectrum keyboard.
//
// Notes:
//      an upper case letter needs caps shift, punctuation generally
//      needs symbol shift. newline maps to ENTER.
//
// Sample call:
//      auto chord = spectrum::chord_for_character('"');
//      // chord->primary == key::p, chord->modifier == key::symbol_shift
//
std::optional<key_chord> chord_for_character(char c);

//
// Returns:
//      a comma separated list of every accepted key name, for error
//      messages and for the tool schema description.
//
std::string key_name_list();

} // namespace spectrum

#endif // SPECTRUM_KEYS_H
