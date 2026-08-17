//
// implementation of the 48K ULA contention model.
//
// the delay is precomputed once into a table with one entry per T-state
// of a frame. the CPU consults it on every bus cycle, so a table lookup
// beats recomputing the raster position each time, and it also makes
// the model easy to dump and compare against published figures.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/contention.h"

namespace spectrum {

namespace {

//
// the stall imposed by the ULA at each offset of an eight T-state
// block. the ULA needs six of every eight T-states to fetch the two
// bitmap bytes and two attribute bytes for the next sixteen pixels, and
// releases the bus for the last two.
//
constexpr u8 block_pattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

//
// the 48K shares only the first RAM bank with the display circuitry.
//
constexpr bool in_contended_bank(u16 address)
{
    return (address & 0xc000) == 0x4000;
}

} // namespace

ula_contention::ula_contention(const machine_timing &timing)
    : timing_(timing),
      delays_(static_cast<std::size_t>(timing.tstates_per_frame()), 0)
{
    const int start = timing_.contention_start_tstate;
    const int line_length = timing_.tstates_per_line;

    for (int line = 0; line < timing_.display_lines; ++line) {
        const int line_start = start + line * line_length;

        for (int column = 0; column < timing_.display_tstates; ++column) {
            const int t = line_start + column;
            if (t < 0 || t >= timing_.tstates_per_frame())
                continue;
            delays_[static_cast<std::size_t>(t)] =
                block_pattern[column % 8];
        }
    }
}

const char *ula_contention::name() const { return "ula-48k"; }

bool ula_contention::is_contended_address(u16 address) const
{
    return in_contended_bank(address);
}

int ula_contention::raw_delay(u32 frame_tstate) const
{
    const auto frame = static_cast<u32>(timing_.tstates_per_frame());
    return delays_[frame_tstate % frame];
}

int ula_contention::memory_delay(u32 frame_tstate, u16 address) const
{
    if (!in_contended_bank(address))
        return 0;
    return raw_delay(frame_tstate);
}

int ula_contention::io_delay(u32 frame_tstate, u16 port) const
{
    // an i/o cycle is four T-states long. how contention distributes
    // over those four depends on two independent facts: whether the
    // port address happens to decode into the contended bank, and
    // whether A0 is low, which is what actually selects the ULA.
    //
    //   contended  A0 low   pattern
    //   no         no       N:4
    //   no         yes      N:1  C:3
    //   yes        no       C:1  C:1  C:1  C:1
    //   yes        yes      C:1  C:3
    //
    // walking the sequence rather than adding a single lookup matters,
    // because each C step samples the table at the time it is reached
    // and so can push the following step into a different block.
    const bool contended = in_contended_bank(port);
    const bool selects_ula = (port & 1) == 0;

    const u32 start = frame_tstate;
    u32 now = frame_tstate;

    const auto contend = [&](int advance) {
        now += static_cast<u32>(raw_delay(now));
        now += static_cast<u32>(advance);
    };

    if (contended) {
        contend(1);
        if (selects_ula) {
            contend(3);
        } else {
            contend(1);
            contend(1);
            contend(1);
        }
    } else if (selects_ula) {
        now += 1;
        contend(3);
    } else {
        now += 4;
    }

    // the caller already accounts for the four base T-states.
    return static_cast<int>(now - start) - 4;
}

const char *no_contention::name() const { return "none"; }

bool no_contention::is_contended_address(u16) const { return false; }

int no_contention::memory_delay(u32, u16) const { return 0; }

int no_contention::io_delay(u32, u16) const { return 0; }

} // namespace spectrum
