//
// fixed width integer aliases shared by the whole spectrum library.
//
// the emulated machine is defined in terms of exact bus widths, so the
// code says u8 and u16 wherever it means "one byte on the data bus" or
// "one address". plain int is reserved for host side counters, which
// keeps accidental width promotion visible at a glance.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_TYPES_H
#define SPECTRUM_TYPES_H

#include <cstdint>

namespace spectrum {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i16 = std::int16_t;
using i32 = std::int32_t;

} // namespace spectrum

#endif // SPECTRUM_TYPES_H
