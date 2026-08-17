//
// the PNG encoder.
//
// the file is taken apart chunk by chunk, its CRCs are recomputed, and
// the image data is inflated and compared against the pixels that went
// in. that is a real round trip rather than a check that the output is
// merely non-empty.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <cstring>
#include <string>
#include <vector>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "png/encoder.h"
#include "test_support.h"

namespace {

std::vector<png::colour> test_palette()
{
    std::vector<png::colour> palette;
    for (int i = 0; i < 16; ++i) {
        const auto level = static_cast<std::uint8_t>(i * 17);
        palette.push_back({level, static_cast<std::uint8_t>(255 - level),
                           static_cast<std::uint8_t>(i)});
    }
    return palette;
}

std::uint32_t read_u32(const std::vector<std::uint8_t> &data,
                       std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

//
// walk the chunk list, checking every CRC, and hand back the
// concatenated IDAT payload.
//
struct parsed_png {
    bool ok = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bit_depth = 0;
    std::uint8_t colour_type = 0;
    std::size_t palette_entries = 0;
    std::vector<std::uint8_t> image_data;
    std::vector<std::string> chunk_order;
    bool crcs_valid = true;
};

parsed_png parse_png(const std::vector<std::uint8_t> &file)
{
    parsed_png result;

    static const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G',
                                              0x0d, 0x0a, 0x1a, 0x0a};
    if (file.size() < 8 || std::memcmp(file.data(), signature, 8) != 0)
        return result;

    std::size_t offset = 8;
    while (offset + 12 <= file.size()) {
        const std::uint32_t length = read_u32(file, offset);
        const std::string type(
            reinterpret_cast<const char *>(file.data() + offset + 4), 4);

        if (offset + 12 + length > file.size())
            return result;

        const std::uint8_t *payload = file.data() + offset + 8;

        const std::uint32_t stored =
            read_u32(file, offset + 8 + length);
        const auto computed = static_cast<std::uint32_t>(
            mz_crc32(MZ_CRC32_INIT, file.data() + offset + 4,
                     length + 4));
        if (stored != computed)
            result.crcs_valid = false;

        result.chunk_order.push_back(type);

        if (type == "IHDR" && length >= 13) {
            result.width = read_u32(file, offset + 8);
            result.height = read_u32(file, offset + 12);
            result.bit_depth = payload[8];
            result.colour_type = payload[9];
        } else if (type == "PLTE") {
            result.palette_entries = length / 3;
        } else if (type == "IDAT") {
            result.image_data.insert(result.image_data.end(), payload,
                                     payload + length);
        }

        offset += 12 + length;

        if (type == "IEND") {
            result.ok = true;
            break;
        }
    }

    return result;
}

void test_structure_and_round_trip()
{
    test::section("file structure and pixel round trip");

    const int width = 13; // odd, so the last byte is half padding
    const int height = 7;

    std::vector<std::uint8_t> pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            pixels.push_back(static_cast<std::uint8_t>((x + y) & 0x0f));
    }

    const std::vector<png::colour> palette = test_palette();
    const std::vector<std::uint8_t> file =
        png::encode_indexed(pixels, width, height, palette, 1);

    test::check(!file.empty(), "an image is produced");

    const parsed_png parsed = parse_png(file);
    test::check(parsed.ok, "the file parses as PNG and ends with IEND");
    test::check(parsed.crcs_valid, "every chunk CRC is correct");
    test::check_eq(parsed.width, width, "the width in IHDR");
    test::check_eq(parsed.height, height, "the height in IHDR");
    test::check_eq(parsed.bit_depth, 4, "four bits per pixel");
    test::check_eq(parsed.colour_type, 3, "colour type 3, indexed");
    test::check_eq(static_cast<long long>(parsed.palette_entries), 16,
                   "sixteen palette entries");

    test::check(parsed.chunk_order.size() >= 4 &&
                    parsed.chunk_order.front() == "IHDR",
                "IHDR comes first");
    test::check(parsed.chunk_order.back() == "IEND", "IEND comes last");

    // inflate the image data and compare it with what went in.
    const std::size_t row_bytes = (static_cast<std::size_t>(width) + 1) / 2;
    const std::size_t expected_size =
        (row_bytes + 1) * static_cast<std::size_t>(height);

    std::vector<std::uint8_t> inflated(expected_size + 64);
    mz_ulong inflated_size = static_cast<mz_ulong>(inflated.size());

    const int status = mz_uncompress(
        inflated.data(), &inflated_size, parsed.image_data.data(),
        static_cast<mz_ulong>(parsed.image_data.size()));

    test::check_eq(status, MZ_OK, "the image data inflates");
    test::check_eq(static_cast<long long>(inflated_size),
                   static_cast<long long>(expected_size),
                   "and is the expected length");

    bool pixels_match = true;
    for (int y = 0; y < height && pixels_match; ++y) {
        const std::size_t row_start =
            static_cast<std::size_t>(y) * (row_bytes + 1);

        if (inflated[row_start] != 0) {
            pixels_match = false; // filter type must be none
            break;
        }

        for (int x = 0; x < width; ++x) {
            const std::uint8_t packed =
                inflated[row_start + 1 +
                         static_cast<std::size_t>(x) / 2];
            const std::uint8_t index =
                (x % 2 == 0) ? static_cast<std::uint8_t>(packed >> 4)
                             : static_cast<std::uint8_t>(packed & 0x0f);
            if (index != pixels[static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x)]) {
                pixels_match = false;
                break;
            }
        }
    }

    test::check(pixels_match,
                "every pixel survives the round trip, padding included");
}

void test_scaling()
{
    test::section("integer scaling");

    const std::vector<std::uint8_t> pixels = {0, 1, 2, 3};
    const std::vector<png::colour> palette = test_palette();

    const std::vector<std::uint8_t> file =
        png::encode_indexed(pixels, 2, 2, palette, 3);

    const parsed_png parsed = parse_png(file);
    test::check(parsed.ok, "a scaled image parses");
    test::check_eq(parsed.width, 6, "the width is tripled");
    test::check_eq(parsed.height, 6, "and so is the height");
}

void test_rejections()
{
    test::section("bad arguments");

    const std::vector<std::uint8_t> pixels(16, 0);
    const std::vector<png::colour> palette = test_palette();

    test::check(png::encode_indexed(pixels, 0, 4, palette, 1).empty(),
                "zero width is refused");
    test::check(png::encode_indexed(pixels, 4, -1, palette, 1).empty(),
                "negative height is refused");
    test::check(png::encode_indexed(pixels, 4, 4, palette, 0).empty(),
                "zero scale is refused");
    test::check(png::encode_indexed(pixels, 4, 4, {}, 1).empty(),
                "an empty palette is refused");
    test::check(png::encode_indexed(pixels, 100, 100, palette, 1).empty(),
                "too few pixels for the stated size is refused");
}

void test_compression_is_worthwhile()
{
    test::section("compression");

    // a Spectrum screen is mostly flat colour, which is exactly what
    // DEFLATE is good at. this is the reason the encoder exists rather
    // than emitting stored blocks.
    const int width = 352;
    const int height = 288;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height),
        7);

    const std::vector<std::uint8_t> file = png::encode_indexed(
        pixels, width, height, test_palette(), 1);

    const std::size_t raw = (static_cast<std::size_t>(width) + 1) / 2 *
                            static_cast<std::size_t>(height);

    test::check(file.size() < raw / 10,
                "a flat screen compresses to under a tenth of its raw "
                "size: " +
                    std::to_string(file.size()) + " from " +
                    std::to_string(raw));
}

} // namespace

int main()
{
    test_structure_and_round_trip();
    test_scaling();
    test_rejections();
    test_compression_is_worthwhile();
    return test::summary("png");
}
