//
// the raster map, beam synchronous rendering, and reading text back.
//
// the interesting case is the last one in test_mid_frame_changes():
// changing the border half way down the screen has to leave the top
// half in the old colour, because those pixels were painted before the
// change happened. an emulator that renders the frame at the end
// instead of as it goes will fail that and pass everything else.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <vector>

#include "spectrum/machine.h"
#include "spectrum/screen_text.h"
#include "test_support.h"

using namespace spectrum;

namespace {

void test_raster_map()
{
    test::section("beam position table");

    machine target;
    const raster_map &raster = target.video().raster();
    const machine_timing &t = target.timing();

    const beam_position &first = raster.at(14336);
    test::check(first.region == beam_region::display,
                "T-state 14336 is on the display");
    test::check_eq(first.x, 48, "and at the left edge of the display");
    test::check_eq(first.y, 48, "on the first display line");
    test::check_eq(first.display_row, 0, "display row 0");
    test::check_eq(first.display_column, 0, "display column 0");

    // the last display pixel pair: last row, last column of the line.
    const u32 last = 14336 + 224 * 191 + 127;
    const beam_position &final_pixel = raster.at(last);
    test::check(final_pixel.region == beam_region::display,
                "the last display T-state is on the display");
    test::check_eq(final_pixel.display_row, 191, "display row 191");
    test::check_eq(final_pixel.x, 48 + 254,
                   "and at the right edge of the display");

    // horizontal retrace paints nothing.
    const beam_position &retrace = raster.at(14336 + 160);
    test::check(retrace.region == beam_region::blanking,
                "horizontal retrace paints nothing");

    // the border either side of a display line.
    const beam_position &right = raster.at(14336 + 130);
    test::check(right.region == beam_region::border,
                "the right border follows the display");

    test::check_eq(t.tstates_per_frame(), 69888, "frame length");
}

void test_display_rendering()
{
    test::section("rendering the display file");

    machine target;
    target.video().set_border(1); // blue

    // a vertical bar in the leftmost character cell, bright yellow ink
    // on blue paper.
    for (int row = 0; row < 192; ++row) {
        const u16 address = static_cast<u16>(
            0x4000 | ((row & 0xc0) << 5) | ((row & 0x07) << 8) |
            ((row & 0x38) << 2));
        target.mem().write(address, 0xf0); // left four pixels set
    }
    for (u16 a = 0x5800; a < 0x5b00; ++a)
        target.mem().write(a, 0x4e); // bright, paper blue, ink yellow

    target.run_frames(2);
    const framebuffer &screen = target.video().screen();

    test::check_eq(screen.pixel(0, 0), 1, "the border is blue");
    test::check_eq(screen.pixel(351, 287), 1,
                   "and blue at the far corner");
    test::check_eq(screen.pixel(48, 48), 14,
                   "the first display pixel is bright yellow ink");
    test::check_eq(screen.pixel(51, 48), 14,
                   "and so is the fourth");
    test::check_eq(screen.pixel(52, 48), 9,
                   "the fifth is bright blue paper");
    test::check_eq(screen.pixel(48, 239), 14,
                   "the bar reaches the last display line");
}

void test_flash()
{
    test::section("the flash attribute");

    machine target;

    for (int row = 0; row < 8; ++row) {
        const u16 address = static_cast<u16>(
            0x4000 | ((row & 0x07) << 8));
        target.mem().write(address, 0xff);
    }
    // flashing, ink white, paper black.
    target.mem().write(0x5800, 0x87);

    target.run_frames(2);
    const u8 first_phase = target.video().screen().pixel(48, 48);

    // the flash cycle swaps ink and paper every sixteen frames.
    target.run_frames(16);
    const u8 second_phase = target.video().screen().pixel(48, 48);

    test::check_eq(first_phase, 7, "the first phase shows the ink");
    test::check_eq(second_phase, 0,
                   "sixteen frames later it shows the paper");
}

//
// For every visible pixel, which T-state of the frame paints it.
//
// Built by walking the raster map once. This is what lets a border test
// assert the exact pixel where a change lands rather than sampling two
// points and hoping.
//
std::vector<u32> paint_times(const machine &target)
{
    const machine_timing &t = target.timing();
    const raster_map &raster = target.video().raster();

    std::vector<u32> when(
        static_cast<std::size_t>(t.screen_width()) *
            static_cast<std::size_t>(t.screen_height()),
        0xffffffffu);

    for (int frame_tstate = 0; frame_tstate < t.tstates_per_frame();
         ++frame_tstate) {
        const beam_position &beam =
            raster.at(static_cast<u32>(frame_tstate));
        if (beam.region == beam_region::blanking)
            continue;

        // each T-state paints two adjacent pixels.
        for (int step = 0; step < 2; ++step) {
            const int x = beam.x + step;
            const int y = beam.y;
            if (x < 0 || y < 0 || x >= t.screen_width() ||
                y >= t.screen_height())
                continue;
            when[static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(t.screen_width()) +
                 static_cast<std::size_t>(x)] =
                static_cast<u32>(frame_tstate);
        }
    }

    return when;
}

void test_mid_frame_border_change()
{
    test::section("mid-frame border change lands on the exact pixel");

    machine target;
    const machine_timing &t = target.timing();
    const std::vector<u32> when = paint_times(target);

    // start a fresh frame so the whole picture belongs to one pass.
    while (target.frame_tstate() != 0)
        target.run_tstates(1);

    target.write_port(0xfe, 2); // red
    const u32 change_at = 14336 + 224 * 96;

    while (target.frame_tstate() < change_at)
        target.run_tstates(1);

    target.write_port(0xfe, 5); // cyan
    const u32 actual_change = target.frame_tstate();

    while (target.frame_tstate() != 0)
        target.run_tstates(1);

    // Every border pixel must carry the colour that was set when the
    // beam passed over it, with no exceptions anywhere on the screen.
    const framebuffer &screen = target.video().screen();
    int wrong = 0;
    int checked = 0;

    for (int y = 0; y < t.screen_height(); ++y) {
        for (const int x : {2, 4, 349, 351}) {
            const u32 painted =
                when[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(t.screen_width()) +
                     static_cast<std::size_t>(x)];
            if (painted == 0xffffffffu)
                continue;

            const u8 expected = painted < actual_change ? 2 : 5;
            ++checked;
            if (screen.pixel(x, y) != expected)
                ++wrong;
        }
    }

    test::check(checked > 500, "a useful number of border pixels tested");
    test::check_eq(wrong, 0,
                   "every border pixel matches the colour in force when "
                   "the beam passed it");
}

//
// The classic border effect: a program on the emulated CPU writing port
// 0xFE in a timed loop paints horizontal stripes down the border.
//
// This only works if the ULA and the CPU really are interleaved at
// T-state granularity. An emulator that renders a frame in one go at
// the end sees only the last OUT of the frame and paints the border a
// single flat colour.
//
void test_program_driven_border_stripes()
{
    test::section("border stripes driven by a Z80 program");

    machine target;

    // loop:  out (0xfe),a   11T
    //        inc a           4T
    //        and 7           7T
    //        ld b,10         7T
    // wait:  djnz wait      13T per pass, 8T on the last
    //        jr loop        12T
    //
    // one pass is 11+4+7+7+(13*9+8)+12 = 166 T-states, so the colour
    // changes several times per 224 T-state scan line and the border
    // fills with bands.
    const std::vector<u8> program = {
        0xd3, 0xfe,       // out (0xfe),a
        0x3c,             // inc a
        0xe6, 0x07,       // and 7
        0x06, 0x0a,       // ld b,10
        0x10, 0xfe,       // djnz $
        0x18, 0xf5,       // jr -11
    };

    target.mem().write_block(0x8000, program, false);
    target.processor().jump(0x8000);

    // let one frame paint completely.
    target.run_frames(2);

    const framebuffer &screen = target.video().screen();

    // count how many different colours appear down the left border.
    bool seen[16] = {};
    int distinct = 0;
    for (int y = 0; y < screen.height(); ++y) {
        const u8 colour = screen.pixel(4, y);
        if (!seen[colour & 0x0f]) {
            seen[colour & 0x0f] = true;
            ++distinct;
        }
    }

    test::check(distinct >= 6,
                "the border shows at least six colours in one frame, "
                "found " + std::to_string(distinct));

    // Bands must be horizontal: the left border of a row is painted
    // within a few T-states, so its pixels share a colour far more
    // often than not.
    int uniform_rows = 0;
    for (int y = 0; y < screen.height(); ++y) {
        const u8 first = screen.pixel(0, y);
        bool uniform = true;
        for (int x = 1; x < 40; ++x) {
            if (screen.pixel(x, y) != first) {
                uniform = false;
                break;
            }
        }
        if (uniform)
            ++uniform_rows;
    }

    test::check(uniform_rows > screen.height() / 2,
                "most rows have a uniform left border, so the bands run "
                "horizontally: " +
                    std::to_string(uniform_rows) + " of " +
                    std::to_string(screen.height()));

    // The same program from the same state must paint the same picture
    // every time, or the timing is not deterministic.
    machine repeat;
    repeat.mem().write_block(0x8000, program, false);
    repeat.processor().jump(0x8000);
    repeat.run_frames(2);

    bool identical = true;
    for (int y = 0; y < screen.height() && identical; ++y) {
        for (int x = 0; x < screen.width(); ++x) {
            if (screen.pixel(x, y) !=
                repeat.video().screen().pixel(x, y)) {
                identical = false;
                break;
            }
        }
    }

    test::check(identical,
                "the same program paints a pixel-identical frame on a "
                "second run");
}

//
// Contention has to change the picture, not just the clock.
//
// The same border loop run from contended memory is stretched by the
// ULA, so its stripes land in different places. If the two frames come
// out identical then contention is not reaching the CPU at all.
//
void test_contention_moves_the_stripes()
{
    test::section("contention shifts a border effect");

    const std::vector<u8> program = {
        0xd3, 0xfe,       // out (0xfe),a
        0x3c,             // inc a
        0xe6, 0x07,       // and 7
        0x06, 0x0a,       // ld b,10
        0x10, 0xfe,       // djnz $
        0x18, 0xf5,       // jr -11
    };

    const auto paint_from = [&program](u16 address) {
        machine target;
        target.mem().write_block(address, program, false);
        target.processor().jump(address);
        target.run_frames(2);

        std::vector<u8> column;
        for (int y = 0; y < target.video().screen().height(); ++y)
            column.push_back(target.video().screen().pixel(4, y));
        return column;
    };

    // 0x9000 is uncontended RAM; 0x5B00 is in the contended bank, just
    // above the display file, so running there is slowed by the ULA.
    const std::vector<u8> uncontended = paint_from(0x9000);
    const std::vector<u8> contended = paint_from(0x5b00);

    int differences = 0;
    for (std::size_t i = 0; i < uncontended.size(); ++i) {
        if (uncontended[i] != contended[i])
            ++differences;
    }

    test::check(differences > 20,
                "running the same loop from contended memory moves the "
                "stripes: " +
                    std::to_string(differences) + " rows differ");
}

void test_screen_text()
{
    test::section("reading the screen as characters");

    machine target;

    // install a two character font: space is blank, 'A' is a solid
    // block. read_screen_text needs space blank and a letter that is
    // neither empty nor full before it trusts a font.
    const u16 font = 0x7000;
    for (int i = 0; i < 8; ++i)
        target.mem().write(static_cast<u16>(font + i), 0x00);
    for (int i = 0; i < 8; ++i) {
        const u16 glyph = static_cast<u16>(font + ('A' - 32) * 8 + i);
        target.mem().write(glyph, 0x3c);
    }

    // draw an 'A' in the top left cell.
    for (int line = 0; line < 8; ++line) {
        const u16 address = static_cast<u16>(
            0x4000 | ((line & 0x07) << 8));
        target.mem().write(address, 0x3c);
    }

    text_options options;
    options.font_address = font;
    const text_screen screen = read_screen_text(target.mem(), options);

    test::check(screen.font_usable, "the font is recognised as usable");
    test::check_eq(static_cast<long long>(screen.rows.size()), 24,
                   "24 rows are returned");
    test::check_eq(static_cast<long long>(screen.rows[0].size()), 32,
                   "each row is 32 characters");
    test::check_eq(screen.rows[0][0], 'A', "the cell reads as 'A'");
    test::check_eq(screen.rows[0][1], ' ', "the next cell is blank");
    test::check_eq(screen.stats.recognised, 1, "one cell was recognised");

    // an inverted cell is the same character with the bits flipped.
    for (int line = 0; line < 8; ++line) {
        const u16 address = static_cast<u16>(
            0x4000 | ((line & 0x07) << 8) | 1);
        target.mem().write(address, static_cast<u8>(~0x3c));
    }
    const text_screen inverted = read_screen_text(target.mem(), options);
    test::check_eq(inverted.rows[0][1], 'A',
                   "an inverse video cell reads as the same character");
    test::check_eq(inverted.stats.inverse, 1, "and is counted as inverse");
}

void test_ascii_art()
{
    test::section("ASCII art rendering");

    machine target;
    target.video().set_border(0); // black
    target.run_frames(1);

    const std::vector<std::string> art =
        render_ascii_art(target.video().screen(), 32, 12);

    test::check_eq(static_cast<long long>(art.size()), 12,
                   "the requested number of rows comes back");
    test::check_eq(static_cast<long long>(art[0].size()), 32,
                   "each row has the requested width");
    test::check_eq(art[0][0], ' ', "a black screen renders as spaces");

    machine bright;
    bright.video().set_border(7); // white
    bright.run_frames(1);
    const std::vector<std::string> lit =
        render_ascii_art(bright.video().screen(), 32, 12);
    test::check(lit[0][0] != ' ',
                "a white border does not render as spaces");
}

} // namespace

int main()
{
    test_raster_map();
    test_display_rendering();
    test_flash();
    test_mid_frame_border_change();
    test_program_driven_border_stripes();
    test_contention_moves_the_stripes();
    test_screen_text();
    test_ascii_art();
    return test::summary("display");
}
