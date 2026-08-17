//
// the rendered image, stored as palette indices rather than as RGB.
//
// the ULA can only ever put one of sixteen colours on screen, so a byte
// per pixel holding an index 0..15 loses nothing. it also keeps the
// beam renderer cheap, lets the PNG encoder emit a 4 bit indexed image,
// and means screen comparisons in tests are exact rather than being at
// the mercy of a colour conversion.
//
// index layout matches the hardware: bits 0..2 are the colour, bit 3 is
// the bright flag, giving black..white then bright black..bright white.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_FRAMEBUFFER_H
#define SPECTRUM_FRAMEBUFFER_H

#include <span>
#include <vector>

#include "spectrum/types.h"

namespace spectrum {

//
// one entry of the hardware palette.
//
struct rgb {
    u8 r;
    u8 g;
    u8 b;
};

//
// Returns:
//      the sixteen ULA colours, indexed the same way the framebuffer
//      indexes them.
//
// Notes:
//      normal colours use 0xD7 rather than 0xFF for an active channel,
//      which is the usual approximation of the analogue levels the ULA
//      actually produced.
//
std::span<const rgb> zx_palette();

//
// Returns: the canonical name of a colour index, such as "bright cyan".
//
const char *colour_name(u8 index);

//
// a rectangular image of palette indices.
//
class framebuffer {
public:
    framebuffer(int width, int height);

    //
    // Set every pixel to one colour.
    //
    void fill(u8 colour);

    //
    // Write one pixel. coordinates outside the image are ignored, which
    // lets the beam renderer stay free of bounds tests.
    //
    void set_pixel(int x, int y, u8 colour);

    //
    // Returns: the pixel, or 0 when the coordinates are outside.
    //
    u8 pixel(int x, int y) const;

    int width() const;
    int height() const;

    //
    // Returns: all pixels in row major order, one byte each.
    //
    std::span<const u8> pixels() const;

private:
    int width_;
    int height_;
    std::vector<u8> pixels_;
};

} // namespace spectrum

#endif // SPECTRUM_FRAMEBUFFER_H
