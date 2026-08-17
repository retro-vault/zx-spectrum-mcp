//
// tools that read the screen back out of the machine.
//
// two views are offered because two questions get asked. "what does it
// look like" wants the rendered frame as a picture, border included,
// exactly as the beam painted it. "what does it say" wants characters,
// and for a display-less server driven by a model that is usually the
// more useful and very much the cheaper answer.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "png/encoder.h"
#include "spectrum/screen_text.h"
#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

//
// convert the emulator palette into the encoder's palette type.
//
std::vector<png::colour> encoder_palette()
{
    std::vector<png::colour> palette;
    palette.reserve(spectrum::zx_palette().size());

    for (const spectrum::rgb &entry : spectrum::zx_palette())
        palette.push_back({entry.r, entry.g, entry.b});

    return palette;
}

//
// cut the pixel display out of the full frame, dropping the border.
//
std::vector<spectrum::u8> crop_display(
    const spectrum::framebuffer &frame,
    const spectrum::machine_timing &timing)
{
    const int x0 = timing.display_origin_x();
    const int y0 = timing.display_origin_y();
    const int width = timing.display_tstates * 2;
    const int height = timing.display_lines;

    std::vector<spectrum::u8> pixels;
    pixels.reserve(static_cast<std::size_t>(width) *
                   static_cast<std::size_t>(height));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            pixels.push_back(frame.pixel(x0 + x, y0 + y));
    }

    return pixels;
}

//
// the screen as a PNG image.
//
class screen_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "screen"; }

    std::string description() const override
    {
        return "Return the current frame as a PNG image. The picture is "
               "whatever the beam has painted, so mid-frame effects "
               "appear exactly as they would on a television. By "
               "default the border is included, giving 352x288 pixels; "
               "without it the image is the 256x192 display alone. For "
               "reading text off the screen, screen_text is far "
               "cheaper.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .boolean("include_border",
                     "Include the border around the display. Default "
                     "true.")
            .integer("scale",
                     "Integer magnification, 1 to 4. Default 1.", 1, 4)
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto scale = read_int_in(arguments["scale"], 1, 4, 1);
        if (!scale)
            return mcp::tool_result::failure(
                "'scale' must be between 1 and 4");

        const bool include_border =
            arguments["include_border"].as_bool(true);

        const spectrum::framebuffer &frame = machine().video().screen();
        const spectrum::machine_timing &timing = machine().timing();
        const std::vector<png::colour> palette = encoder_palette();

        std::vector<spectrum::u8> pixels;
        int width = 0;
        int height = 0;

        if (include_border) {
            const std::span<const spectrum::u8> source = frame.pixels();
            pixels.assign(source.begin(), source.end());
            width = frame.width();
            height = frame.height();
        } else {
            pixels = crop_display(frame, timing);
            width = timing.display_tstates * 2;
            height = timing.display_lines;
        }

        const std::vector<std::uint8_t> file = png::encode_indexed(
            pixels, width, height, palette, static_cast<int>(*scale));

        if (file.empty())
            return mcp::tool_result::failure(
                "the PNG encoder failed to produce an image");

        const std::string encoded = mcp::base64_encode(std::string_view(
            reinterpret_cast<const char *>(file.data()), file.size()));

        char summary[256];
        std::snprintf(summary, sizeof summary,
                      "frame %llu at T-state %u: %dx%d pixels, border "
                      "colour %s, %zu bytes of PNG",
                      static_cast<unsigned long long>(
                          machine().frame_number()),
                      machine().frame_tstate(),
                      width * static_cast<int>(*scale),
                      height * static_cast<int>(*scale),
                      spectrum::colour_name(machine().video().border()),
                      file.size());

        mcp::tool_result result;
        result.content.push_back(mcp::content_block::image(
            encoded, "image/png"));
        result.content.push_back(mcp::content_block::text(summary));

        json::value structured = json::value::make_object();
        structured.set("width",
                       json::value(width * static_cast<int>(*scale)));
        structured.set("height",
                       json::value(height * static_cast<int>(*scale)));
        structured.set("frame", json::value(machine().frame_number()));
        structured.set("border",
                       json::value(machine().video().border()));
        structured.set("bytes", json::value(static_cast<std::int64_t>(
                                    file.size())));
        result.structured = std::move(structured);

        return result;
    }
};

//
// the screen as text.
//
class screen_text_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "screen_text"; }

    std::string description() const override
    {
        return "Read the screen as text. In 'chars' mode each 8x8 cell "
               "is matched against the character set the machine is "
               "using, found through the CHARS system variable, giving "
               "24 rows of 32 characters; inverse video is recognised "
               "and cells that match nothing become '?'. In 'ascii' "
               "mode the picture is rendered as ASCII art by "
               "brightness, which suits graphics rather than text. "
               "Character matching needs a real ROM or a program "
               "supplied font; check 'font_usable' in the result.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("mode",
                    "'chars' to recognise characters, 'ascii' for "
                    "ASCII art. Default 'chars'.",
                    {"chars", "ascii"})
            .integer("font_address",
                     "Bitmap address of character 32, overriding the "
                     "CHARS system variable.",
                     0, 0xffff)
            .integer("columns",
                     "ASCII art width in characters. Default 64.", 8,
                     200)
            .integer("rows",
                     "ASCII art height in characters. Default 24.", 4,
                     100)
            .boolean("trim",
                     "Strip trailing spaces from each row. Default "
                     "true.")
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const std::string mode = arguments["mode"].as_string("chars");
        const bool trim = arguments["trim"].as_bool(true);

        if (mode == "ascii")
            return render_ascii(arguments, trim);

        if (mode != "chars")
            return mcp::tool_result::failure("unknown mode '" + mode +
                                             "'");

        spectrum::text_options options;

        if (!arguments["font_address"].is_null()) {
            const auto font =
                read_int_in(arguments["font_address"], 0, 0xffff);
            if (!font)
                return mcp::tool_result::failure(
                    "'font_address' must be between 0 and 65535");
            options.font_address = static_cast<spectrum::u16>(*font);
        }

        const spectrum::text_screen screen =
            spectrum::read_screen_text(machine().mem(), options);

        json::value rows = json::value::make_array();
        std::string text;

        for (const std::string &row : screen.rows) {
            const std::string line = trim ? trimmed(row) : row;
            rows.push_back(json::value(line));
            if (!text.empty())
                text += '\n';
            text += line;
        }

        json::value structured = json::value::make_object();
        structured.set("mode", json::value("chars"));
        structured.set("rows", std::move(rows));
        structured.set("font_address",
                       json::value(screen.font_address));
        structured.set("font_usable", json::value(screen.font_usable));
        structured.set("recognised",
                       json::value(screen.stats.recognised));
        structured.set("inverse", json::value(screen.stats.inverse));
        structured.set("blank", json::value(screen.stats.blank));
        structured.set("unknown", json::value(screen.stats.unknown));

        if (!screen.font_usable) {
            text += "\n\n[no usable character set at " +
                    hex16(screen.font_address) +
                    "; load a ROM or pass font_address, or use mode "
                    "'ascii']";
        }

        return mcp::tool_result::of(std::move(text),
                                    std::move(structured));
    }

private:
    static std::string trimmed(const std::string &row)
    {
        std::size_t end = row.size();
        while (end > 0 && row[end - 1] == ' ')
            --end;
        return row.substr(0, end);
    }

    mcp::tool_result render_ascii(const json::value &arguments,
                                  bool trim)
    {
        const auto columns = read_int_in(arguments["columns"], 8, 200, 64);
        const auto rows = read_int_in(arguments["rows"], 4, 100, 24);

        if (!columns || !rows)
            return mcp::tool_result::failure(
                "'columns' must be 8 to 200 and 'rows' 4 to 100");

        const std::vector<std::string> art = spectrum::render_ascii_art(
            machine().video().screen(), static_cast<int>(*columns),
            static_cast<int>(*rows));

        json::value lines = json::value::make_array();
        std::string text;

        for (const std::string &row : art) {
            const std::string line = trim ? trimmed(row) : row;
            lines.push_back(json::value(line));
            if (!text.empty())
                text += '\n';
            text += line;
        }

        json::value structured = json::value::make_object();
        structured.set("mode", json::value("ascii"));
        structured.set("rows", std::move(lines));
        structured.set("columns", json::value(*columns));
        structured.set("frame", json::value(machine().frame_number()));

        return mcp::tool_result::of(std::move(text),
                                    std::move(structured));
    }
};

} // namespace

void register_screen_tools(mcp::tool_registry &registry,
                           spectrum::machine &target)
{
    registry.add(std::make_unique<screen_tool>(target));
    registry.add(std::make_unique<screen_text_tool>(target));
}

void register_all_tools(mcp::tool_registry &registry,
                        spectrum::machine &target)
{
    register_load_tool(registry, target);
    register_tape_tools(registry, target);
    register_machine_tools(registry, target);
    register_memory_tools(registry, target);
    register_cpu_tools(registry, target);
    register_io_tools(registry, target);
    register_screen_tools(registry, target);
    register_capture_tools(registry, target);
}

} // namespace tools
