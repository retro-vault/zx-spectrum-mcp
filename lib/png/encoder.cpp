//
// implementation of the indexed PNG writer.
//
// PNG stores pixels as a series of scan lines, each preceded by a
// filter byte, with the whole lot compressed as one zlib stream. filter
// type 0, "none", is used throughout: the images are flat blocks of
// colour and DEFLATE already handles them well, so the filters would
// cost time without saving space.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "png/encoder.h"

#include <cstring>

// only the mz_ prefixed entry points are used. without this, miniz also
// defines static inline aliases called crc32, adler32 and friends, none
// of which are referenced here, and every one of them warns under
// -Wall -Wextra.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

namespace png {

namespace {

constexpr std::uint8_t signature[8] = {0x89, 'P', 'N', 'G',
                                       0x0d, 0x0a, 0x1a, 0x0a};

// PNG colour type 3 means each pixel is an index into PLTE.
constexpr std::uint8_t colour_type_indexed = 3;
constexpr std::uint8_t bit_depth = 4;

void put_u32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

//
// append a complete chunk: length, type, payload, CRC.
//
// the CRC covers the type and the payload but not the length, which is
// a detail easy to get wrong and impossible to spot without a decoder.
//
void put_chunk(std::vector<std::uint8_t> &out, const char type[4],
               std::span<const std::uint8_t> payload)
{
    put_u32(out, static_cast<std::uint32_t>(payload.size()));

    const std::size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());

    const auto crc = static_cast<std::uint32_t>(
        mz_crc32(MZ_CRC32_INIT, out.data() + crc_start,
                 out.size() - crc_start));
    put_u32(out, crc);
}

//
// build the raw scan lines: a filter byte then two pixels per byte.
//
std::vector<std::uint8_t> build_raw_lines(
    std::span<const std::uint8_t> pixels, int width, int height,
    std::size_t palette_size, int scale)
{
    const int scaled_width = width * scale;
    const int scaled_height = height * scale;
    const std::size_t row_bytes =
        (static_cast<std::size_t>(scaled_width) + 1) / 2;

    std::vector<std::uint8_t> raw;
    raw.reserve((row_bytes + 1) * static_cast<std::size_t>(scaled_height));

    const auto clamp_index = [palette_size](std::uint8_t index) {
        return static_cast<std::uint8_t>(
            index < palette_size ? index : palette_size - 1);
    };

    for (int y = 0; y < scaled_height; ++y) {
        raw.push_back(0); // filter type: none

        const auto source_row = static_cast<std::size_t>(y / scale);
        const std::uint8_t *line =
            pixels.data() + source_row * static_cast<std::size_t>(width);

        // two 4 bit pixels are packed per byte, high nibble first. an
        // odd width leaves the last low nibble as padding.
        std::uint8_t pending = 0;
        bool have_high = false;

        for (int x = 0; x < scaled_width; ++x) {
            const std::uint8_t index = clamp_index(line[x / scale]);

            if (!have_high) {
                pending = static_cast<std::uint8_t>(index << 4);
                have_high = true;
            } else {
                raw.push_back(static_cast<std::uint8_t>(pending | index));
                have_high = false;
            }
        }

        if (have_high)
            raw.push_back(pending);
    }

    return raw;
}

} // namespace

std::vector<std::uint8_t> encode_indexed(
    std::span<const std::uint8_t> pixels, int width, int height,
    std::span<const colour> palette, int scale)
{
    if (width <= 0 || height <= 0 || scale <= 0 || palette.empty())
        return {};
    if (palette.size() > max_palette_size)
        return {};
    if (pixels.size() < static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height))
        return {};

    const std::vector<std::uint8_t> raw =
        build_raw_lines(pixels, width, height, palette.size(), scale);

    // compress the scan lines into the zlib stream IDAT carries.
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::uint8_t> compressed(bound);

    const int status =
        mz_compress2(compressed.data(), &bound, raw.data(),
                     static_cast<mz_ulong>(raw.size()), MZ_BEST_COMPRESSION);
    if (status != MZ_OK)
        return {};

    compressed.resize(bound);

    std::vector<std::uint8_t> file;
    file.insert(file.end(), std::begin(signature), std::end(signature));

    std::vector<std::uint8_t> header;
    put_u32(header, static_cast<std::uint32_t>(width * scale));
    put_u32(header, static_cast<std::uint32_t>(height * scale));
    header.push_back(bit_depth);
    header.push_back(colour_type_indexed);
    header.push_back(0); // compression method: deflate
    header.push_back(0); // filter method: adaptive
    header.push_back(0); // interlace method: none
    put_chunk(file, "IHDR", header);

    std::vector<std::uint8_t> plte;
    plte.reserve(palette.size() * 3);
    for (const colour &entry : palette) {
        plte.push_back(entry.r);
        plte.push_back(entry.g);
        plte.push_back(entry.b);
    }
    put_chunk(file, "PLTE", plte);

    put_chunk(file, "IDAT", compressed);
    put_chunk(file, "IEND", {});

    return file;
}

} // namespace png
