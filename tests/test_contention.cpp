//
// the shape of the 48K contention table and the i/o contention rules.
//
// test_cpu_timing.cpp checks that the table is applied correctly; this
// one checks that the table itself says the right thing.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/contention.h"
#include "test_support.h"

using namespace spectrum;

namespace {

const machine_timing &geometry() { return zx_spectrum_48k_timing; }

void test_frame_geometry()
{
    test::section("frame geometry");

    const machine_timing &t = geometry();
    test::check_eq(t.tstates_per_frame(), 69888, "69888 T-states a frame");
    test::check_eq(t.first_display_tstate(), 14336,
                   "the first display pixel is drawn at 14336");
    test::check_eq(t.contention_start_tstate, 14335,
                   "contention starts one T-state before that");
    test::check_eq(t.screen_width(), 352, "352 pixels wide with border");
    test::check_eq(t.screen_height(), 288, "288 lines with border");
    test::check_eq(t.frames_per_second(), 50, "about 50 frames a second");
}

void test_delay_pattern()
{
    test::section("the 6,5,4,3,2,1,0,0 pattern");

    const ula_contention model(geometry());
    const int expected[8] = {6, 5, 4, 3, 2, 1, 0, 0};

    for (int offset = 0; offset < 8; ++offset) {
        test::check_eq(model.raw_delay(14335 + offset), expected[offset],
                       "offset " + std::to_string(offset) +
                           " of the first block");
    }

    for (int offset = 0; offset < 8; ++offset) {
        test::check_eq(model.raw_delay(14343 + offset), expected[offset],
                       "offset " + std::to_string(offset) +
                           " of the second block");
    }
}

void test_delay_boundaries()
{
    test::section("where the table is quiet");

    const ula_contention model(geometry());
    const machine_timing &t = geometry();

    test::check_eq(model.raw_delay(0), 0, "vertical blanking is free");
    test::check_eq(model.raw_delay(14334), 0,
                   "the T-state before contention starts is free");

    // 128 T-states of display, then border and retrace to the end of
    // the line.
    for (int offset = 128; offset < t.tstates_per_line; ++offset) {
        if (model.raw_delay(static_cast<u32>(14335 + offset)) != 0) {
            test::check(false, "line offset " + std::to_string(offset) +
                                   " outside the display should be free");
            return;
        }
    }
    test::check(true, "the whole border and retrace of a line is free");

    // the last display line, then nothing after it.
    const u32 last_line_start =
        static_cast<u32>(14335 + 191 * t.tstates_per_line);
    test::check_eq(model.raw_delay(last_line_start), 6,
                   "the last display line is still contended");
    test::check_eq(
        model.raw_delay(static_cast<u32>(
            14335 + 192 * t.tstates_per_line)),
        0, "the line after the display is free");
}

void test_address_range()
{
    test::section("which addresses are contended");

    const ula_contention model(geometry());

    test::check(model.is_contended_address(0x4000), "0x4000 is contended");
    test::check(model.is_contended_address(0x7fff), "0x7FFF is contended");
    test::check(!model.is_contended_address(0x3fff),
                "ROM at 0x3FFF is not");
    test::check(!model.is_contended_address(0x8000), "0x8000 is not");
    test::check(!model.is_contended_address(0xffff), "0xFFFF is not");

    test::check_eq(model.memory_delay(14335, 0x8000), 0,
                   "an uncontended address is never delayed");
    test::check_eq(model.memory_delay(14335, 0x4000), 6,
                   "a contended address takes the table value");
}

void test_io_contention()
{
    test::section("i/o contention, the four cases");

    const ula_contention model(geometry());

    // The pattern depends on whether the port address decodes into the
    // contended bank and on whether A0 is low, which is what selects
    // the ULA. During vertical blanking every case must come out free,
    // because each C step samples a table full of zeroes.
    test::check_eq(model.io_delay(0, 0xfe), 0,
                   "ULA port in blanking is free");
    test::check_eq(model.io_delay(0, 0x4000), 0,
                   "contended high byte in blanking is free");
    test::check_eq(model.io_delay(0, 0xff), 0,
                   "an unclaimed odd port in blanking is free");

    // Inside the display the ULA port must be delayed, and by more than
    // a single sample, because the sequence contends twice.
    const int ula_in_display = model.io_delay(14335, 0xfe);
    test::check(ula_in_display > 0,
                "the ULA port is delayed during the display");

    const int contended_odd = model.io_delay(14335, 0x40ff);
    test::check(contended_odd > 0,
                "an odd port in the contended bank is delayed four "
                "times over");

    // An odd port outside the contended bank touches neither the ULA
    // nor contended memory, so it is the one case that is always free.
    test::check_eq(model.io_delay(14335, 0x80ff), 0,
                   "an odd port outside the contended bank is always "
                   "free");
}

void test_no_contention_strategy()
{
    test::section("the null strategy");

    const no_contention model;

    test::check_eq(model.memory_delay(14335, 0x4000), 0,
                   "memory is never delayed");
    test::check_eq(model.io_delay(14335, 0xfe), 0,
                   "i/o is never delayed");
    test::check(!model.is_contended_address(0x4000),
                "no address is contended");
}

} // namespace

int main()
{
    test_frame_geometry();
    test_delay_pattern();
    test_delay_boundaries();
    test_address_range();
    test_io_contention();
    test_no_contention_strategy();
    return test::summary("contention");
}
