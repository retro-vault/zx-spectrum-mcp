//
// construction of the T-state to beam position table.
//
// a line is transmitted in four phases:
//
//      columns   0 .. 127   the 256 pixel display
//      columns 128 .. 151   right border
//      columns 152 .. 199   horizontal retrace, nothing is painted
//      columns 200 .. 223   left border of the *following* row
//
// during the top and bottom border the first phase paints border colour
// instead of display, but the phase boundaries are unchanged.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/raster.h"

namespace spectrum {

namespace {

constexpr beam_position blank_position{
    beam_region::blanking, -1, -1, -1, -1};

} // namespace

raster_map::raster_map(const machine_timing &timing) : timing_(timing)
{
    const int frame = timing_.tstates_per_frame();
    const int line_length = timing_.tstates_per_line;
    const int display_end = timing_.display_tstates;
    const int right_end = display_end + timing_.border_right_tstates;
    const int left_start = line_length - timing_.border_left_tstates;

    const int first_visible = timing_.first_visible_line();
    const int last_visible = timing_.last_visible_line();
    const int first_display = timing_.first_display_line;
    const int last_display = first_display + timing_.display_lines;

    table_.assign(static_cast<std::size_t>(frame), blank_position);

    for (int t = 0; t < frame; ++t) {
        const int line = t / line_length;
        const int column = t % line_length;

        int target_line = line;
        int x = -1;

        if (column < display_end) {
            x = timing_.display_origin_x() + column * 2;
        } else if (column < right_end) {
            x = timing_.display_origin_x() + display_end * 2 +
                (column - display_end) * 2;
        } else if (column >= left_start) {
            // the tail of a line already belongs to the next row.
            target_line = line + 1;
            x = (column - left_start) * 2;
        } else {
            continue; // horizontal retrace
        }

        if (target_line < first_visible || target_line >= last_visible)
            continue; // vertical blanking

        beam_position &slot = table_[static_cast<std::size_t>(t)];
        slot.x = static_cast<i16>(x);
        slot.y = static_cast<i16>(target_line - first_visible);

        // only the first phase of a line, and only on a line that
        // carries the display, shows display file contents.
        const bool on_display_line =
            target_line >= first_display && target_line < last_display;

        if (column < display_end && on_display_line) {
            slot.region = beam_region::display;
            slot.display_column = static_cast<i16>(column);
            slot.display_row =
                static_cast<i16>(target_line - first_display);
        } else {
            slot.region = beam_region::border;
            slot.display_column = -1;
            slot.display_row = -1;
        }
    }
}

const beam_position &raster_map::at(u32 frame_tstate) const
{
    return table_[frame_tstate % table_.size()];
}

const machine_timing &raster_map::timing() const { return timing_; }

} // namespace spectrum
