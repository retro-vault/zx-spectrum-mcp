//
// implementation of the palette indexed framebuffer.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/framebuffer.h"

#include <array>

namespace spectrum {

namespace {

// an inactive channel is off, a normal channel sits below full scale,
// and only the bright flag drives a channel all the way up.
constexpr u8 lo = 0x00;
constexpr u8 md = 0xd7;
constexpr u8 hi = 0xff;

constexpr std::array<rgb, 16> palette = {{
    {lo, lo, lo}, // 0  black
    {lo, lo, md}, // 1  blue
    {md, lo, lo}, // 2  red
    {md, lo, md}, // 3  magenta
    {lo, md, lo}, // 4  green
    {lo, md, md}, // 5  cyan
    {md, md, lo}, // 6  yellow
    {md, md, md}, // 7  white
    {lo, lo, lo}, // 8  bright black, identical to black on real hardware
    {lo, lo, hi}, // 9  bright blue
    {hi, lo, lo}, // 10 bright red
    {hi, lo, hi}, // 11 bright magenta
    {lo, hi, lo}, // 12 bright green
    {lo, hi, hi}, // 13 bright cyan
    {hi, hi, lo}, // 14 bright yellow
    {hi, hi, hi}, // 15 bright white
}};

constexpr const char *palette_names[16] = {
    "black",        "blue",        "red",         "magenta",
    "green",        "cyan",        "yellow",      "white",
    "bright black", "bright blue", "bright red",  "bright magenta",
    "bright green", "bright cyan", "bright yellow", "bright white",
};

} // namespace

std::span<const rgb> zx_palette() { return palette; }

const char *colour_name(u8 index)
{
    return palette_names[index & 0x0f];
}

framebuffer::framebuffer(int width, int height)
    : width_(width),
      height_(height),
      pixels_(static_cast<std::size_t>(width) *
                  static_cast<std::size_t>(height),
              0)
{
}

void framebuffer::fill(u8 colour)
{
    std::fill(pixels_.begin(), pixels_.end(), colour);
}

void framebuffer::set_pixel(int x, int y, u8 colour)
{
    // silently drop out of range writes: the beam renderer paints two
    // pixels at a time and the right hand one can fall past the edge.
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return;

    pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
            static_cast<std::size_t>(x)] = colour;
}

u8 framebuffer::pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return 0;

    return pixels_[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(width_) +
                   static_cast<std::size_t>(x)];
}

int framebuffer::width() const { return width_; }

int framebuffer::height() const { return height_; }

std::span<const u8> framebuffer::pixels() const { return pixels_; }

} // namespace spectrum
