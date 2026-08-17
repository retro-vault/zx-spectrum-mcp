//
// raster and clock geometry for the emulated machine.
//
// every timing constant the emulator depends on is gathered here in one
// plain struct rather than scattered through the ULA. a different
// machine variant is then a different set of numbers, not a different
// code path, which is what makes the 48K model swappable later.
//
// the numbers below describe the 48K ULA. one frame is 312 lines of 224
// T-states, so 69888 T-states, and the CPU runs at 3.5 MHz, giving the
// familiar 50.08 Hz frame rate.
//
// the frame origin is the T-state at which the ULA raises /INT. the
// first pixel of the display area is therefore drawn at T-state 14336,
// which is exactly line 64 column 0, and memory contention begins one
// T-state earlier at 14335 because the ULA fetches ahead of the beam.
//
// a line runs display, right border, horizontal retrace, left border,
// in that order. the left border at the end of a line already belongs
// to the *next* visible row, and raster_map is what untangles that.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_TIMING_H
#define SPECTRUM_TIMING_H

#include "spectrum/types.h"

namespace spectrum {

//
// clock and raster geometry of one machine variant.
//
// the accessors are constexpr one liners on purpose: they are derived
// quantities, and keeping them in the header lets the raster tables be
// sized at compile time.
//
struct machine_timing {
    // T-states in one scan line, including all blanking.
    int tstates_per_line;

    // scan lines in one frame, including vertical blanking.
    int lines_per_frame;

    // scan lines carrying the 256x192 pixel display.
    int display_lines;

    // T-states of one line spent on the pixel display.
    int display_tstates;

    // first line of the pixel display, counted from the interrupt.
    int first_display_line;

    // first T-state at which contention is applied.
    int contention_start_tstate;

    // how long /INT stays asserted at the start of a frame.
    int interrupt_tstates;

    // border T-states either side of the display within a line.
    int border_left_tstates;
    int border_right_tstates;

    // border lines above and below the display that we render.
    int border_top_lines;
    int border_bottom_lines;

    // cpu clock in hz.
    int cpu_hz;

    // Returns: total T-states in one frame.
    constexpr int tstates_per_frame() const
    {
        return tstates_per_line * lines_per_frame;
    }

    // Returns: T-state at which the top left display pixel is drawn.
    constexpr int first_display_tstate() const
    {
        return first_display_line * tstates_per_line;
    }

    // Returns: first scan line included in the rendered image.
    constexpr int first_visible_line() const
    {
        return first_display_line - border_top_lines;
    }

    // Returns: one past the last scan line included in the image.
    constexpr int last_visible_line() const
    {
        return first_display_line + display_lines + border_bottom_lines;
    }

    // Returns: rendered image width in pixels. each T-state of the
    // visible part of a line paints two pixels.
    constexpr int screen_width() const
    {
        return (border_left_tstates + display_tstates +
                border_right_tstates) * 2;
    }

    // Returns: rendered image height in pixels.
    constexpr int screen_height() const
    {
        return border_top_lines + display_lines + border_bottom_lines;
    }

    // Returns: horizontal pixel offset at which the display starts.
    constexpr int display_origin_x() const
    {
        return border_left_tstates * 2;
    }

    // Returns: vertical pixel offset at which the display starts.
    constexpr int display_origin_y() const { return border_top_lines; }

    // Returns: frames per second, rounded, for reporting only.
    constexpr int frames_per_second() const
    {
        return cpu_hz / tstates_per_frame();
    }
};

//
// timing of the 48K ZX Spectrum ULA (ferranti 5C102E).
//
inline constexpr machine_timing zx_spectrum_48k_timing{
    .tstates_per_line = 224,
    .lines_per_frame = 312,
    .display_lines = 192,
    .display_tstates = 128,
    .first_display_line = 64,
    .contention_start_tstate = 14335,
    .interrupt_tstates = 32,
    .border_left_tstates = 24,
    .border_right_tstates = 24,
    .border_top_lines = 48,
    .border_bottom_lines = 48,
    .cpu_hz = 3500000,
};

} // namespace spectrum

#endif // SPECTRUM_TIMING_H
