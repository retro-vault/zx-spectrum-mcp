//
// minimal PNG writer for palette indexed images.
//
// the emulator only ever produces sixteen colours, so the encoder emits
// a 4 bit indexed PNG with a PLTE chunk rather than converting to RGB
// first. that is a quarter of the pixel data before compression, and
// after compression a typical Spectrum screen lands in a couple of
// kilobytes. it matters because the image travels base64 encoded inside
// an MCP response and ends up in a language model's context window.
//
// only what PNG requires is implemented: signature, IHDR, PLTE, IDAT,
// IEND, no filtering, no interlacing, no ancillary chunks. DEFLATE and
// CRC come from the vendored miniz.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef PNG_ENCODER_H
#define PNG_ENCODER_H

#include <cstdint>
#include <span>
#include <vector>

namespace png {

//
// one palette colour.
//
struct colour {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

//
// largest palette a 4 bit indexed image can carry.
//
inline constexpr std::size_t max_palette_size = 16;

//
// Encode an indexed image as a PNG file.
//
// Parameters:
//      pixels      - one byte per pixel holding a palette index, in row
//                    major order. must hold width * height entries.
//                    indices at or above the palette size are clamped.
//      width       - image width in pixels, must be positive.
//      height      - image height in pixels, must be positive.
//      palette     - up to 16 colours.
//      scale       - integer magnification, nearest neighbour. 1 leaves
//                    the image alone.
//
// Returns:
//      the complete PNG file, or an empty vector when the arguments do
//      not describe a valid image.
//
// Notes:
//      the result is a byte stream, not text. base64 encode it before
//      putting it in a JSON message.
//
// Sample call:
//      auto file = png::encode_indexed(fb.pixels(), fb.width(),
//                                      fb.height(), palette, 1);
//
std::vector<std::uint8_t> encode_indexed(
    std::span<const std::uint8_t> pixels, int width, int height,
    std::span<const colour> palette, int scale = 1);

} // namespace png

#endif // PNG_ENCODER_H
