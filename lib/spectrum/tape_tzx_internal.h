//
// TZX decoder details shared by the control-flow and sampled-data units.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_TAPE_TZX_INTERNAL_H
#define SPECTRUM_TAPE_TZX_INTERNAL_H

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "spectrum/types.h"
#include "tape_internal.h"

namespace spectrum::tape_internal {

struct tzx_block {
    u8 id = 0;
    std::size_t file_offset = 0;
    std::vector<u8> body;
};

inline u16 tzx_u16(std::span<const u8> data, std::size_t offset)
{
    return static_cast<u16>(data[offset] | (data[offset + 1] << 8));
}

inline u32 tzx_u24(std::span<const u8> data, std::size_t offset)
{
    return static_cast<u32>(data[offset] | (data[offset + 1] << 8) |
                            (data[offset + 2] << 16));
}

inline u32 tzx_u32(std::span<const u8> data, std::size_t offset)
{
    return static_cast<u32>(data[offset]) |
           (static_cast<u32>(data[offset + 1]) << 8) |
           (static_cast<u32>(data[offset + 2]) << 16) |
           (static_cast<u32>(data[offset + 3]) << 24);
}

bool append_tzx_csw(builder &out, const tzx_block &block,
                    std::string &error);
bool append_tzx_generalized(builder &out, const tzx_block &block,
                            std::string &error);

} // namespace spectrum::tape_internal

#endif // SPECTRUM_TAPE_TZX_INTERNAL_H
