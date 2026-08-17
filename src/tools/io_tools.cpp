//
// tools for the keyboard and the i/o ports.
//
// press_keys does not merely set bits in the matrix; it holds them for
// a while and then lets go, running the machine in between. it has to.
// the ROM samples the keyboard once per frame from the interrupt
// routine and applies its own debounce, so a key that goes down and up
// without any frames passing is never seen at all.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <memory>

#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

// long enough for the ROM's scan and debounce to accept a key.
constexpr std::int64_t default_hold_frames = 3;

// released for long enough that the next keystroke reads as new.
constexpr std::int64_t default_gap_frames = 2;

// keeps one call from running for minutes.
constexpr std::int64_t max_keystrokes = 256;

//
// one keystroke: a key with the shifts that must be held with it.
//
struct keystroke {
    std::vector<spectrum::key> keys;
    std::string label;
};

//
// type text, or press a chord of named keys.
//
class press_keys_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "press_keys"; }

    std::string description() const override
    {
        return "Press keys on the Spectrum keyboard, running the "
               "machine so the ROM can see them. Give 'text' to type a "
               "string, where upper case adds caps shift and "
               "punctuation adds symbol shift automatically, or 'keys' "
               "to hold a set of named keys together as one chord. Key "
               "names are letters, digits, ENTER, SPACE, CAPS_SHIFT and "
               "SYMBOL_SHIFT. A newline in 'text' presses ENTER. This "
               "advances emulated time.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("text",
                    "Text to type, one character at a time.")
            .string_array("keys",
                          "Key names to hold down together, for "
                          "example ['CAPS_SHIFT','1'].")
            .integer("hold_frames",
                     "Frames each keystroke is held. Default 3.", 1,
                     100)
            .integer("gap_frames",
                     "Frames with all keys released between "
                     "keystrokes. Default 2.",
                     0, 100)
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const bool has_text = arguments["text"].is_string();
        const bool has_keys = arguments["keys"].is_array();

        if (has_text == has_keys)
            return mcp::tool_result::failure(
                "supply exactly one of 'text' or 'keys'");

        const auto hold = read_int_in(arguments["hold_frames"], 1, 100,
                                      default_hold_frames);
        const auto gap = read_int_in(arguments["gap_frames"], 0, 100,
                                     default_gap_frames);
        if (!hold || !gap)
            return mcp::tool_result::failure(
                "'hold_frames' must be 1 to 100 and 'gap_frames' 0 to "
                "100");

        std::vector<keystroke> strokes;

        if (has_text) {
            const std::string text = arguments["text"].as_string();

            if (static_cast<std::int64_t>(text.size()) > max_keystrokes)
                return mcp::tool_result::failure(
                    "'text' is limited to " +
                    std::to_string(max_keystrokes) +
                    " characters per call");

            for (const char c : text) {
                const auto chord = spectrum::chord_for_character(c);
                if (!chord) {
                    return mcp::tool_result::failure(
                        std::string("cannot type character '") + c +
                        "' on a Spectrum keyboard");
                }

                keystroke stroke;
                if (chord->modifier)
                    stroke.keys.push_back(*chord->modifier);
                stroke.keys.push_back(chord->primary);
                stroke.label = std::string(1, c);
                strokes.push_back(std::move(stroke));
            }
        } else {
            keystroke stroke;

            for (const json::value &item : arguments["keys"].elements()) {
                if (!item.is_string())
                    return mcp::tool_result::failure(
                        "'keys' must be an array of key name strings");

                const std::string wanted = item.as_string();
                const auto key = spectrum::key_from_name(wanted);
                if (!key) {
                    return mcp::tool_result::failure(
                        "unknown key '" + wanted +
                        "'; valid names are: " +
                        spectrum::key_name_list());
                }

                if (!stroke.label.empty())
                    stroke.label += '+';
                stroke.label += spectrum::key_name(*key);
                stroke.keys.push_back(*key);
            }

            if (stroke.keys.empty())
                return mcp::tool_result::failure("'keys' is empty");

            strokes.push_back(std::move(stroke));
        }

        const spectrum::u64 before = machine().total_tstates();
        std::string typed;

        for (const keystroke &stroke : strokes) {
            for (const spectrum::key k : stroke.keys)
                machine().keys().press(k);

            machine().run_frames(static_cast<spectrum::u64>(*hold));

            machine().keys().release_all();

            if (*gap > 0)
                machine().run_frames(static_cast<spectrum::u64>(*gap));

            typed += stroke.label;
        }

        const spectrum::u64 elapsed =
            machine().total_tstates() - before;

        json::value structured = machine_state();
        structured.set("keystrokes", json::value(static_cast<std::int64_t>(
                                         strokes.size())));
        structured.set("tstates_elapsed", json::value(elapsed));

        return mcp::tool_result::of(
            "pressed " + std::to_string(strokes.size()) +
                " keystroke(s) [" + typed + "] over " +
                std::to_string(elapsed) + " T-states",
            std::move(structured));
    }
};

//
// read a port directly.
//
class read_port_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "read_port"; }

    std::string description() const override
    {
        return "Read an I/O port without running the CPU. Port "
               "decoding is partial on this machine, so the whole 16 "
               "bit address matters: 0xFEFE reads the caps shift half "
               "row, 0x7FFE reads the space half row. A port no device "
               "claims returns the ULA floating bus value. No emulated "
               "time passes and no contention is applied.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("port", "Full 16 bit port address, 0 to 65535.", 0,
                     0xffff)
            .required({"port"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto port = read_int_in(arguments["port"], 0, 0xffff);
        if (!port)
            return mcp::tool_result::failure(
                "'port' is required and must be between 0 and 65535");

        const auto address = static_cast<spectrum::u16>(*port);
        const spectrum::u8 value = machine().read_port(address);

        const spectrum::io_device *device =
            machine().ports().device_for(address);

        json::value structured = json::value::make_object();
        structured.set("port", json::value(address));
        structured.set("value", json::value(value));
        structured.set("device", json::value(device != nullptr
                                                 ? device->name()
                                                 : "floating bus"));

        return mcp::tool_result::of(
            "port " + hex16(address) + " reads " + hex8(value) +
                " (from " +
                (device != nullptr ? device->name() : "the floating bus") +
                ")",
            std::move(structured));
    }
};

//
// write a port directly.
//
class set_port_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "set_port"; }

    std::string description() const override
    {
        return "Write an I/O port without running the CPU. Writing an "
               "even port reaches the ULA, where bits 0 to 2 set the "
               "border colour, bit 3 is the tape output and bit 4 the "
               "speaker. No emulated time passes.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("port", "Full 16 bit port address, 0 to 65535.", 0,
                     0xffff)
            .integer("value", "Byte to write, 0 to 255.", 0, 0xff)
            .required({"port", "value"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto port = read_int_in(arguments["port"], 0, 0xffff);
        if (!port)
            return mcp::tool_result::failure(
                "'port' is required and must be between 0 and 65535");

        const auto value = read_int_in(arguments["value"], 0, 0xff);
        if (!value)
            return mcp::tool_result::failure(
                "'value' is required and must be between 0 and 255");

        const auto address = static_cast<spectrum::u16>(*port);
        machine().write_port(address,
                             static_cast<spectrum::u8>(*value));

        json::value structured = json::value::make_object();
        structured.set("port", json::value(address));
        structured.set("value", json::value(*value));
        structured.set("border",
                       json::value(machine().video().border()));

        return mcp::tool_result::of("wrote " + hex8(*value) +
                                        " to port " + hex16(address),
                                    std::move(structured));
    }
};

} // namespace

void register_io_tools(mcp::tool_registry &registry,
                       spectrum::machine &target)
{
    registry.add(std::make_unique<press_keys_tool>(target));
    registry.add(std::make_unique<read_port_tool>(target));
    registry.add(std::make_unique<set_port_tool>(target));
}

} // namespace tools
