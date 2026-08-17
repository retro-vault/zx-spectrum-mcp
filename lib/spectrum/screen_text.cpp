//
// implementation of screen text recognition and ASCII art rendering.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/screen_text.h"

#include <algorithm>
#include <array>

namespace spectrum {

namespace {

// the ROM character set covers these codes, eight bytes each.
constexpr int first_char = 32;
constexpr int last_char = 127;
constexpr int char_height = 8;

//
// address of one pixel line of the display file. the row bits are
// interleaved; see ula.cpp for the full explanation.
//
u16 bitmap_address(int pixel_row, int column)
{
    return static_cast<u16>(memory::screen_base |
                            ((pixel_row & 0xc0) << 5) |
                            ((pixel_row & 0x07) << 8) |
                            ((pixel_row & 0x38) << 2) | column);
}

//
// read the eight bytes of one character cell.
//
std::array<u8, char_height> read_cell(const memory &mem, int row,
                                      int column)
{
    std::array<u8, char_height> cell{};
    for (int line = 0; line < char_height; ++line)
        cell[static_cast<std::size_t>(line)] =
            mem.read(bitmap_address(row * char_height + line, column));
    return cell;
}

//
// read the eight bytes of one character's bitmap from the font.
//
std::array<u8, char_height> read_glyph(const memory &mem, u16 font,
                                       int code)
{
    const auto base =
        static_cast<u16>(font + (code - first_char) * char_height);

    std::array<u8, char_height> glyph{};
    for (int line = 0; line < char_height; ++line)
        glyph[static_cast<std::size_t>(line)] =
            mem.read(static_cast<u16>(base + line));
    return glyph;
}

bool is_blank(const std::array<u8, char_height> &cell)
{
    return std::all_of(cell.begin(), cell.end(),
                       [](u8 b) { return b == 0; });
}

//
// decide whether what the font pointer points at is plausibly a
// character set.
//
// with no ROM loaded the area is uniform filler, and matching against
// it would turn every cell into the same wrong answer. space must be
// blank and at least one letter must have some but not all bits set.
//
bool font_looks_usable(const memory &mem, u16 font)
{
    if (!is_blank(read_glyph(mem, font, ' ')))
        return false;

    for (int code = 'A'; code <= 'Z'; ++code) {
        const auto glyph = read_glyph(mem, font, code);
        const bool any_set = std::any_of(glyph.begin(), glyph.end(),
                                         [](u8 b) { return b != 0; });
        const bool all_set = std::all_of(glyph.begin(), glyph.end(),
                                         [](u8 b) { return b == 0xff; });
        if (any_set && !all_set)
            return true;
    }

    return false;
}

//
// resolve the font address from the CHARS system variable.
//
// CHARS points 256 bytes below the bitmap of character 32, which is the
// offset the ROM's printing routine expects.
//
u16 resolve_font(const memory &mem)
{
    const auto low = mem.read(chars_sysvar);
    const auto high = mem.read(static_cast<u16>(chars_sysvar + 1));
    const auto chars = static_cast<u16>(low | (high << 8));

    if (chars == 0)
        return static_cast<u16>(default_chars + 256);

    return static_cast<u16>(chars + 256);
}

//
// relative brightness of each palette entry, 0..255.
//
u8 luminance(const rgb &colour)
{
    // integer approximation of the usual luma weights.
    return static_cast<u8>((colour.r * 77 + colour.g * 151 +
                            colour.b * 28) >>
                           8);
}

} // namespace

text_screen read_screen_text(const memory &mem,
                             const text_options &options)
{
    text_screen result;
    result.font_address =
        options.font_address != 0 ? options.font_address
                                  : resolve_font(mem);
    result.font_usable = font_looks_usable(mem, result.font_address);

    // cache the font once; otherwise every cell would re-read it.
    std::array<std::array<u8, char_height>, last_char - first_char + 1>
        glyphs{};
    for (int code = first_char; code <= last_char; ++code)
        glyphs[static_cast<std::size_t>(code - first_char)] =
            read_glyph(mem, result.font_address, code);

    result.rows.reserve(text_rows);

    for (int row = 0; row < text_rows; ++row) {
        std::string line;
        line.reserve(text_columns);

        for (int column = 0; column < text_columns; ++column) {
            const auto cell = read_cell(mem, row, column);

            if (is_blank(cell)) {
                line.push_back(' ');
                ++result.stats.blank;
                continue;
            }

            char found = 0;
            bool inverted = false;

            for (int code = first_char; code <= last_char; ++code) {
                const auto &glyph =
                    glyphs[static_cast<std::size_t>(code - first_char)];

                if (cell == glyph) {
                    found = static_cast<char>(code);
                    break;
                }

                if (options.detect_inverse) {
                    bool matches = true;
                    for (std::size_t i = 0; i < cell.size(); ++i) {
                        if (cell[i] != static_cast<u8>(~glyph[i])) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        found = static_cast<char>(code);
                        inverted = true;
                        break;
                    }
                }
            }

            if (found != 0) {
                line.push_back(found);
                if (inverted)
                    ++result.stats.inverse;
                else
                    ++result.stats.recognised;
            } else {
                line.push_back(options.unknown);
                ++result.stats.unknown;
            }
        }

        result.rows.push_back(std::move(line));
    }

    return result;
}

std::vector<std::string> render_ascii_art(const framebuffer &image,
                                          int columns, int rows)
{
    std::vector<std::string> output;
    if (columns <= 0 || rows <= 0 || image.width() <= 0 ||
        image.height() <= 0)
        return output;

    // dark to light; the eye reads this as increasing brightness in a
    // monospaced terminal.
    static constexpr char ramp[] = " .:-=+*#%@";
    constexpr int ramp_size = static_cast<int>(sizeof(ramp)) - 2;

    const std::span<const rgb> palette = zx_palette();

    output.reserve(static_cast<std::size_t>(rows));

    for (int row = 0; row < rows; ++row) {
        std::string line;
        line.reserve(static_cast<std::size_t>(columns));

        const int y0 = row * image.height() / rows;
        const int y1 = std::max((row + 1) * image.height() / rows, y0 + 1);

        for (int column = 0; column < columns; ++column) {
            const int x0 = column * image.width() / columns;
            const int x1 =
                std::max((column + 1) * image.width() / columns, x0 + 1);

            // average the brightness of every pixel in the cell.
            unsigned total = 0;
            unsigned count = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const u8 index = image.pixel(x, y);
                    total += luminance(palette[index & 0x0f]);
                    ++count;
                }
            }

            const unsigned mean = count != 0 ? total / count : 0;
            const int slot = static_cast<int>(mean * ramp_size / 255);
            line.push_back(ramp[std::clamp(slot, 0, ramp_size)]);
        }

        output.push_back(std::move(line));
    }

    return output;
}

} // namespace spectrum
