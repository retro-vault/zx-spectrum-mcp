//
// the ULA: video generation, port 0xFE, and the frame interrupt.
//
// the ULA is ticked once per T-state, in step with the CPU, and paints
// the two pixels the beam covers during that T-state. nothing is
// deferred to the end of the frame. that is what makes mid-frame
// effects work: a program that changes the border colour or rewrites
// the display file while the beam is running gets exactly the picture
// the hardware would have produced, because the pixels above the beam
// have already been painted and cannot be retroactively changed.
//
// display bytes are latched in eight T-state blocks, matching the real
// fetch cycle. within each block the ULA collects two bitmap bytes and
// two attribute bytes, which together describe the next sixteen pixels,
// and it needs six of the eight T-states to do it. those same six
// T-states are what the CPU is held off the bus for, so the fetch
// schedule here and the contention table in contention.h are two
// descriptions of one piece of hardware.
//
// the keyboard is read through this class rather than being a device of
// its own, because on real hardware the ULA is what decodes port 0xFE
// and merges the five key bits with the tape input.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_ULA_H
#define SPECTRUM_ULA_H

#include "spectrum/framebuffer.h"
#include "spectrum/io_device.h"
#include "spectrum/keyboard.h"
#include "spectrum/memory.h"
#include "spectrum/raster.h"
#include "spectrum/timing.h"
#include "spectrum/types.h"

namespace spectrum {

//
// how many frames each half of the flash cycle lasts.
//
inline constexpr int flash_period_frames = 16;

//
// video and port 0xFE.
//
class ula final : public io_device {
public:
    //
    // Parameters:
    //      timing      - raster geometry to render with.
    //      mem         - memory the display file is read from.
    //      kbd         - key matrix reported through port 0xFE.
    //
    // Notes:
    //      both references must outlive the ULA.
    //
    ula(const machine_timing &timing, memory &mem, keyboard &kbd);

    //
    // Return to power on state: white border, flash phase cleared, and
    // a blank image.
    //
    void reset();

    //
    // Start a new frame. advances the frame counter and the flash
    // cycle, and drops the display latch.
    //
    void begin_frame();

    //
    // Paint the two pixels covered during one T-state.
    //
    // Parameters:
    //      frame_tstate    - T-states since the frame interrupt.
    //
    void tick(u32 frame_tstate);

    //
    // Returns:
    //      true while the ULA is holding /INT low at the top of the
    //      frame.
    //
    bool interrupt_active(u32 frame_tstate) const;

    //
    // Returns: the rendered image, display plus border.
    //
    const framebuffer &screen() const;

    //
    // Returns: the beam position table in use.
    //
    const raster_map &raster() const;

    // border colour, 0..7.
    u8 border() const;
    void set_border(u8 colour);

    //
    // Returns: true while flashing attributes show swapped colours.
    //
    bool flash_phase() const;

    //
    // Returns: frames completed since reset.
    //
    u32 frames_rendered() const;

    // speaker and tape output bits of the last write to port 0xFE.
    bool speaker() const;
    bool mic() const;

    // tape input level, reported in bit 6 of port 0xFE.
    bool ear_input() const;
    void set_ear_input(bool level);

    //
    // The byte the ULA is currently driving onto the data bus.
    //
    // Parameters:
    //      frame_tstate    - T-states since the frame interrupt.
    //
    // Returns:
    //      the display byte being fetched, or 0xFF when the ULA is not
    //      fetching.
    //
    // Notes:
    //      this is the floating bus. reading an unclaimed port picks it
    //      up, and beam synchronised games use it to tell where the
    //      raster is.
    //
    u8 floating_bus(u32 frame_tstate) const;

    const char *name() const override;
    bool handles(u16 port) const override;
    u8 read(u16 port, u32 frame_tstate) override;
    void write(u16 port, u8 value, u32 frame_tstate) override;

private:
    //
    // the four bytes the ULA collects during one eight T-state block:
    // two bitmap bytes and the two attributes that colour them.
    //
    struct display_fetch {
        u8 bitmap[2];
        u8 attribute[2];
    };

    display_fetch fetch_block(int row, int block_column) const;
    void ensure_latched(int row, int column);
    void paint_display(const beam_position &beam);
    void paint_border(const beam_position &beam);

    machine_timing timing_;
    memory &memory_;
    keyboard &keyboard_;
    raster_map raster_;
    framebuffer screen_;

    u8 border_ = 7;
    bool speaker_ = false;
    bool mic_ = false;
    bool ear_input_ = false;

    u32 frames_ = 0;
    int flash_counter_ = 0;
    bool flash_phase_ = false;

    display_fetch latch_{};
    int latched_row_ = -1;
    int latched_block_ = -1;
};

} // namespace spectrum

#endif // SPECTRUM_ULA_H
