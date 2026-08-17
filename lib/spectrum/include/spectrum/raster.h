//
// where the electron beam is at any T-state of the frame.
//
// this is the table that makes beam synchronous rendering possible. the
// ULA paints two pixels per T-state, so given the T-state counter alone
// the renderer must know which two pixels, and whether they belong to
// the border, to the pixel display, or to blanking.
//
// the awkward part is that a scan line is transmitted as display, right
// border, horizontal retrace, left border. the left border at the tail
// of a line is already the left edge of the *next* visible row, so the
// mapping from T-state to row is not simply t / tstates_per_line. the
// table resolves that once at construction and the hot path becomes a
// single indexed load.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_RASTER_H
#define SPECTRUM_RASTER_H

#include <vector>

#include "spectrum/timing.h"
#include "spectrum/types.h"

namespace spectrum {

//
// what the beam is doing during one T-state.
//
enum class beam_region : u8 {
    // outside the rendered image: retrace or vertical blanking.
    blanking,

    // border, painted in the current border colour.
    border,

    // the 256x192 pixel display, painted from the display file.
    display
};

//
// the two pixels painted during one T-state.
//
struct beam_position {
    beam_region region;

    // x of the left pixel of the pair, and the row, both in image
    // coordinates. -1 while blanking.
    i16 x;
    i16 y;

    // position within the pixel display: which T-column of the 128, and
    // which of the 192 pixel rows. both -1 unless region is display.
    i16 display_column;
    i16 display_row;
};

//
// precomputed beam position for every T-state of a frame.
//
class raster_map {
public:
    explicit raster_map(const machine_timing &timing);

    //
    // Look up the beam position.
    //
    // Parameters:
    //      frame_tstate    - T-states since the frame interrupt. values
    //                        beyond one frame wrap around.
    //
    // Returns:
    //      what the beam paints during that T-state.
    //
    const beam_position &at(u32 frame_tstate) const;

    //
    // Returns: the geometry this table was built from.
    //
    const machine_timing &timing() const;

private:
    machine_timing timing_;
    std::vector<beam_position> table_;
};

} // namespace spectrum

#endif // SPECTRUM_RASTER_H
