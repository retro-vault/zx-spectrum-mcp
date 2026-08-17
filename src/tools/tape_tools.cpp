//
// Cassette transport control and tape image diagnostics.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <memory>

#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

json::value structured_status(const spectrum::tape_status &tape)
{
    json::value out = json::value::make_object();
    out.set("loaded", json::value(tape.loaded));
    out.set("playing", json::value(tape.playing));
    out.set("finished", json::value(tape.finished));
    out.set("stopped_by_command", json::value(tape.stopped_by_command));
    out.set("ear_level", json::value(tape.ear_level));
    out.set("format", json::value(tape.format));
    out.set("title", json::value(tape.title));
    out.set("image_bytes", json::value(
                               static_cast<std::int64_t>(tape.image_bytes)));
    out.set("blocks", json::value(
                          static_cast<std::int64_t>(tape.blocks)));
    out.set("data_blocks", json::value(
                               static_cast<std::int64_t>(tape.data_blocks)));
    out.set("segments", json::value(
                            static_cast<std::int64_t>(tape.segments)));
    out.set("segment", json::value(
                           static_cast<std::int64_t>(tape.segment)));
    out.set("duration_tstates", json::value(tape.duration_tstates));
    out.set("position_tstates", json::value(tape.position_tstates));
    return out;
}

std::string summary(const spectrum::tape_status &tape)
{
    if (!tape.loaded)
        return "no tape is inserted";

    std::string state = tape.playing
                            ? "playing"
                            : tape.finished ? "finished" : "stopped";
    if (tape.stopped_by_command)
        state = "stopped by a TZX command";
    return tape.format + " tape, " + std::to_string(tape.blocks) +
           " blocks and " + std::to_string(tape.duration_tstates) +
           " T-states: " + state + " at " +
           std::to_string(tape.position_tstates);
}

class tape_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "tape"; }

    std::string description() const override
    {
        return "Control the TAP or TZX cassette transport. Use status, "
               "play/resume, stop, rewind, or eject. Tape time advances "
               "only while the emulated machine runs.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("action", "Transport action. Default status.",
                    {"status", "play", "stop", "rewind", "eject"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const std::string action = arguments["action"].as_string("status");
        if (action == "play") {
            if (!machine().tape().play())
                return mcp::tool_result::failure(
                    "cannot play: no tape is inserted or it is at the end");
        } else if (action == "stop") {
            machine().tape().stop();
        } else if (action == "rewind") {
            if (!machine().tape().status().loaded)
                return mcp::tool_result::failure(
                    "cannot rewind: no tape is inserted");
            machine().tape().rewind();
        } else if (action == "eject") {
            machine().tape().eject();
        } else if (action != "status") {
            return mcp::tool_result::failure("unknown tape action '" +
                                             action + "'");
        }

        const spectrum::tape_status tape = machine().tape().status();
        return mcp::tool_result::of(summary(tape), structured_status(tape));
    }
};

} // namespace

void register_tape_tools(mcp::tool_registry &registry,
                         spectrum::machine &target)
{
    registry.add(std::make_unique<tape_tool>(target));
}

} // namespace tools
