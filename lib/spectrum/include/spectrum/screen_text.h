//
// reading the screen back as text.
//
// this is the tool that makes a display-less emulator usable. an agent
// driving the machine mostly wants to know what it says, not what it
// looks like, and a 32x24 grid of characters is a far better answer
// than a PNG for that question.
//
// recognition works by matching each 8x8 cell against the character
// bitmaps the machine itself is using, found through the CHARS system
// variable. that is deliberately not a hardcoded table: it means a
// program that redefines the character set, or draws with user defined
// graphics, is still read correctly, and it avoids shipping a copy of
// the copyrighted Sinclair font.
//
// ASCII art rendering is offered as well, for the screens that are
// pictures rather than text.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_SCREEN_TEXT_H
#define SPECTRUM_SCREEN_TEXT_H

#include <string>
#include <vector>

#include "spectrum/framebuffer.h"
#include "spectrum/memory.h"
#include "spectrum/types.h"

namespace spectrum {

//
// address of the CHARS system variable, which points 256 bytes below
// the bitmap for character 32.
//
inline constexpr u16 chars_sysvar = 0x5c36;

//
// where the Sinclair ROM keeps its font, used when CHARS is not set up.
//
inline constexpr u16 default_chars = 0x3c00;

// the character grid is fixed by the display file layout.
inline constexpr int text_columns = 32;
inline constexpr int text_rows = 24;

//
// how to read the screen.
//
struct text_options {
    // bitmap address of character 32. zero resolves it from CHARS.
    u16 font_address = 0;

    // substituted for a cell that matches no character.
    char unknown = '?';

    // also match cells whose bits are inverted, as used for the cursor
    // and for highlighted text.
    bool detect_inverse = true;
};

//
// how the recognition went, so a caller can judge the result.
//
struct text_statistics {
    int blank = 0;
    int recognised = 0;
    int inverse = 0;
    int unknown = 0;
};

//
// the screen as text.
//
struct text_screen {
    // 24 rows of 32 characters.
    std::vector<std::string> rows;

    // font actually used.
    u16 font_address = 0;

    // false when the font does not look like a character set, for
    // instance because no ROM is loaded. rows will be mostly unknown.
    bool font_usable = false;

    text_statistics stats;
};

//
// Read the display file as characters.
//
// Parameters:
//      mem         - memory holding the display file and the font.
//      options     - recognition settings.
//
// Returns:
//      the recognised text and how confident it is.
//
// Sample call:
//      auto screen = spectrum::read_screen_text(m.mem());
//      for (const auto &line : screen.rows) puts(line.c_str());
//
text_screen read_screen_text(const memory &mem,
                             const text_options &options = {});

//
// Render the image as ASCII art.
//
// Parameters:
//      image       - the rendered frame.
//      columns     - output width in characters.
//      rows        - output height in characters.
//
// Returns:
//      one string per row, using a ramp from space to '@' by
//      brightness.
//
// Notes:
//      brightness comes from the palette, so this reflects what is on
//      screen rather than what is in the display file. it is the right
//      answer for a screen that is a picture.
//
std::vector<std::string> render_ascii_art(const framebuffer &image,
                                          int columns, int rows);

} // namespace spectrum

#endif // SPECTRUM_SCREEN_TEXT_H
