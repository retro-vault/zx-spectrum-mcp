//
// implementation of the ULA.
//
// the display file is not laid out linearly. its address bits are
// interleaved so that the ULA can walk a character row by incrementing
// one address register, which was cheap in gates and awkward for
// everyone since. bitmap_address() below is that interleave.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/ula.h"

#include <utility>

namespace spectrum {

namespace {

//
// address of the bitmap byte for one character cell.
//
// the pixel row splits three ways: bits 6..7 select which third of the
// screen, bits 3..5 select the character row inside that third, and
// bits 0..2 select the pixel line inside the character.
//
u16 bitmap_address(int row, int char_x)
{
    return static_cast<u16>(memory::screen_base |
                            ((row & 0xc0) << 5) |
                            ((row & 0x07) << 8) |
                            ((row & 0x38) << 2) | char_x);
}

//
// address of the attribute byte for one character cell. attributes are
// laid out linearly, one per 8x8 cell.
//
u16 attribute_address(int row, int char_x)
{
    return static_cast<u16>(memory::attribute_base + (row >> 3) * 32 +
                            char_x);
}

} // namespace

ula::ula(const machine_timing &timing, memory &mem, keyboard &kbd)
    : timing_(timing),
      memory_(mem),
      keyboard_(kbd),
      raster_(timing),
      screen_(timing.screen_width(), timing.screen_height())
{
    reset();
}

void ula::reset()
{
    border_ = 7;
    speaker_ = false;
    mic_ = false;
    ear_input_ = false;
    frames_ = 0;
    flash_counter_ = 0;
    flash_phase_ = false;
    latched_row_ = -1;
    latched_block_ = -1;
    screen_.fill(border_);
}

void ula::begin_frame()
{
    ++frames_;

    if (++flash_counter_ >= flash_period_frames) {
        flash_counter_ = 0;
        flash_phase_ = !flash_phase_;
    }

    // a new frame starts a new fetch; nothing carries over.
    latched_row_ = -1;
    latched_block_ = -1;
}

ula::display_fetch ula::fetch_block(int row, int block_column) const
{
    // one block covers eight T-states, so sixteen pixels, so the two
    // adjacent character cells starting at this column.
    const int char_x = block_column / 4;

    display_fetch result{};
    for (int i = 0; i < 2; ++i) {
        const int x = char_x + i;
        result.bitmap[i] = memory_.read(bitmap_address(row, x));
        result.attribute[i] = memory_.read(attribute_address(row, x));
    }
    return result;
}

void ula::ensure_latched(int row, int column)
{
    const int block = column & ~7;

    // normally this hits only on the first T-state of a block, but
    // testing it every time keeps the renderer correct when the beam is
    // entered part way through a block, as happens after a snapshot
    // restores a mid-frame T-state counter.
    if (latched_row_ == row && latched_block_ == block)
        return;

    latch_ = fetch_block(row, block);
    latched_row_ = row;
    latched_block_ = block;
}

void ula::paint_border(const beam_position &beam)
{
    screen_.set_pixel(beam.x, beam.y, border_);
    screen_.set_pixel(beam.x + 1, beam.y, border_);
}

void ula::paint_display(const beam_position &beam)
{
    const int row = beam.display_row;
    const int column = beam.display_column;

    ensure_latched(row, column);

    // within the block, the first four T-states paint the first fetched
    // byte and the last four the second.
    const int offset = column % 8;
    const int half = offset / 4;
    const int pair = offset % 4;

    const u8 bits = latch_.bitmap[half];
    const u8 attribute = latch_.attribute[half];

    u8 ink = attribute & 0x07;
    u8 paper = static_cast<u8>((attribute >> 3) & 0x07);

    if (attribute & 0x40) {
        ink = static_cast<u8>(ink + 8);
        paper = static_cast<u8>(paper + 8);
    }

    // a flashing cell swaps ink and paper for half the flash cycle.
    if ((attribute & 0x80) && flash_phase_)
        std::swap(ink, paper);

    const u8 left_mask = static_cast<u8>(0x80 >> (pair * 2));
    const u8 right_mask = static_cast<u8>(0x80 >> (pair * 2 + 1));

    screen_.set_pixel(beam.x, beam.y, (bits & left_mask) ? ink : paper);
    screen_.set_pixel(beam.x + 1, beam.y,
                      (bits & right_mask) ? ink : paper);
}

void ula::tick(u32 frame_tstate)
{
    const beam_position &beam = raster_.at(frame_tstate);

    switch (beam.region) {
    case beam_region::blanking:
        return;
    case beam_region::border:
        paint_border(beam);
        return;
    case beam_region::display:
        paint_display(beam);
        return;
    }
}

bool ula::interrupt_active(u32 frame_tstate) const
{
    return frame_tstate < static_cast<u32>(timing_.interrupt_tstates);
}

const framebuffer &ula::screen() const { return screen_; }

const raster_map &ula::raster() const { return raster_; }

u8 ula::border() const { return border_; }

void ula::set_border(u8 colour) { border_ = static_cast<u8>(colour & 7); }

bool ula::flash_phase() const { return flash_phase_; }

u32 ula::frames_rendered() const { return frames_; }

bool ula::speaker() const { return speaker_; }

bool ula::mic() const { return mic_; }

bool ula::ear_input() const { return ear_input_; }

void ula::set_ear_input(bool level) { ear_input_ = level; }

u8 ula::floating_bus(u32 frame_tstate) const
{
    const beam_position &beam = raster_.at(frame_tstate);
    if (beam.region != beam_region::display)
        return 0xff;

    // the ULA drives the bus only while it is actually fetching, which
    // is the first four T-states of each block; for the rest it has
    // what it needs and lets go.
    const int offset = beam.display_column % 8;
    if (offset >= 4)
        return 0xff;

    const display_fetch fetched =
        fetch_block(beam.display_row, beam.display_column & ~7);

    switch (offset) {
    case 0: return fetched.bitmap[0];
    case 1: return fetched.attribute[0];
    case 2: return fetched.bitmap[1];
    default: return fetched.attribute[1];
    }
}

const char *ula::name() const { return "ula"; }

bool ula::handles(u16 port) const
{
    // the ULA decodes a single address line: any even port is ours.
    return (port & 1) == 0;
}

u8 ula::read(u16 port, u32)
{
    // bits 0..4 are the key matrix, bit 6 is the tape input, and the
    // two unused lines float high.
    u8 result = static_cast<u8>(keyboard_.scan(port) & 0x1f);
    result |= 0xa0;
    if (ear_input_)
        result |= 0x40;
    return result;
}

void ula::write(u16, u8 value, u32)
{
    border_ = static_cast<u8>(value & 0x07);
    mic_ = (value & 0x08) != 0;
    speaker_ = (value & 0x10) != 0;
}

} // namespace spectrum
