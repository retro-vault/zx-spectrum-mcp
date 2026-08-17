//
// the load tool: getting a program or a machine state into the emulator.
//
// kept apart from the other machine tools because the format dispatch,
// the choice between a file and inline bytes, and the ROM special case
// add up to more than the rest of them together.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <memory>

#include "spectrum/snapshot.h"
#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

//
// load a program or a machine state.
//
class load_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "load"; }

    std::string description() const override
    {
        return "Load a program or machine state into the emulator. "
               "Supply either 'path' to read a file, or 'data' for "
               "inline bytes. Formats: binary (raw bytes at 'address'), "
               "scr (6912-byte screen dump), sna (48K snapshot), z80 "
               "(snapshot versions 1-3), tap and tzx (cassette images "
               "played through the ULA EAR input). When 'format' is omitted it is "
               "guessed from the file extension, defaulting to binary. "
               "A ROM image can be supplied with format 'rom'.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("path", "File to load from disk.")
            .string("data",
                    "Inline bytes as hex digit pairs, for example "
                    "'3e02cd0000'. Use instead of 'path' for short "
                    "programs.")
            .string("format",
                    "Image format. Omit to guess from the path.",
                    {"binary", "scr", "sna", "z80", "tap", "tzx", "rom"})
            .integer("address",
                     "Where binary data is placed. Default 32768 "
                     "(0x8000), the usual start of free RAM.",
                     0, 0xffff)
            .integer("start",
                     "Set the program counter here after a binary "
                     "load. Omit to leave it alone.",
                     0, 0xffff)
            .boolean("reset",
                     "Reset the machine and clear RAM before loading. "
                     "Snapshots always reset regardless.")
            .boolean("autoplay",
                     "Start TAP or TZX playback immediately. Default "
                     "true; ignored by other formats.")
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        std::vector<spectrum::u8> image;

        const bool has_path = arguments["path"].is_string();
        const bool has_data = !arguments["data"].is_null();

        if (has_path == has_data) {
            return mcp::tool_result::failure(
                "supply exactly one of 'path' or 'data'");
        }

        std::string source;

        if (has_path) {
            source = arguments["path"].as_string();
            std::string error;
            auto contents = spectrum::read_file(source, error);
            if (!contents)
                return mcp::tool_result::failure(error);
            image = std::move(*contents);
        } else {
            auto bytes = read_bytes(arguments["data"]);
            if (!bytes) {
                return mcp::tool_result::failure(
                    "'data' must be an array of byte values or a string "
                    "of hex digit pairs");
            }
            image = std::move(*bytes);
            source = "inline data";
        }

        // a ROM is not a snapshot format; it replaces the lower 16K
        // rather than being loaded into the address space.
        const std::string requested =
            arguments["format"].as_string("");

        if (requested == "rom") {
            machine().mem().load_rom(image);
            machine().reset(true);

            json::value structured = json::value::make_object();
            structured.set("format", json::value("rom"));
            structured.set("bytes",
                           json::value(static_cast<std::int64_t>(
                               image.size())));

            return mcp::tool_result::of(
                "loaded a " + std::to_string(image.size()) +
                    " byte ROM image from " + source +
                    " and reset the machine",
                std::move(structured));
        }

        spectrum::load_options options;

        if (!requested.empty()) {
            const auto format =
                spectrum::snapshot_format_from_name(requested);
            if (!format) {
                return mcp::tool_result::failure(
                    "unknown format '" + requested + "'");
            }
            options.format = *format;
        } else if (has_path) {
            options.format =
                spectrum::snapshot_format_from_path(source).value_or(
                    spectrum::snapshot_format::binary);
        } else {
            options.format = spectrum::snapshot_format::binary;
        }

        const auto address =
            read_int_in(arguments["address"], 0, 0xffff, 0x8000);
        if (!address)
            return mcp::tool_result::failure(
                "'address' must be between 0 and 65535");
        options.address = static_cast<spectrum::u16>(*address);

        if (!arguments["start"].is_null()) {
            const auto start =
                read_int_in(arguments["start"], 0, 0xffff);
            if (!start)
                return mcp::tool_result::failure(
                    "'start' must be between 0 and 65535");
            options.start = static_cast<spectrum::u16>(*start);
        }

        options.reset_first = arguments["reset"].as_bool(false);
        options.autoplay = arguments["autoplay"].as_bool(true);

        const spectrum::load_result result =
            spectrum::load_into(machine(), image, options);

        if (!result.ok)
            return mcp::tool_result::failure(result.error);

        json::value structured = json::value::make_object();
        structured.set("format",
                       json::value(spectrum::snapshot_format_name(
                           options.format)));
        structured.set("bytes", json::value(static_cast<std::int64_t>(
                                    result.bytes)));
        structured.set("entry", json::value(result.entry));
        structured.set("source", json::value(source));
        if (options.format == spectrum::snapshot_format::tap ||
            options.format == spectrum::snapshot_format::tzx) {
            const spectrum::tape_status tape = machine().tape().status();
            structured.set("playing", json::value(tape.playing));
            structured.set("blocks", json::value(
                                         static_cast<std::int64_t>(
                                             tape.blocks)));
            structured.set("duration_tstates",
                           json::value(tape.duration_tstates));
        }

        return mcp::tool_result::of(result.description + " (from " +
                                        source + ")",
                                    std::move(structured));
    }
};

//
// restart the machine.
} // namespace

void register_load_tool(mcp::tool_registry &registry,
                       spectrum::machine &target)
{
    registry.add(std::make_unique<load_tool>(target));
}

} // namespace tools
