//
// tools that get code into the machine and make it run.
//
// the running tools all share one idea: a call must terminate. every
// one of them ends up in machine::run() with at least one limit set, so
// a program that never reaches its target still returns an answer
// saying so rather than hanging the server on the end of a pipe.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <cstdio>
#include <memory>

#include "spectrum/snapshot.h"
#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

//
// the longest a single call may run, about twenty seconds of emulated
// time. a caller who genuinely wants more can ask again.
//
constexpr spectrum::u64 max_tstates_per_call = 70000000;

//
// default when a run is asked for with no limit at all.
//
constexpr spectrum::u64 default_run_frames = 1;

//
// summarise how a run ended, in the form a reader wants first.
//
std::string describe_run(const spectrum::run_result &result)
{
    char buffer[256];
    std::snprintf(buffer, sizeof buffer,
                  "%s after %llu T-states (%llu instructions, %llu "
                  "frames); pc is now %s",
                  spectrum::stop_reason_name(result.reason),
                  static_cast<unsigned long long>(result.tstates),
                  static_cast<unsigned long long>(result.instructions),
                  static_cast<unsigned long long>(result.frames),
                  hex16(result.pc).c_str());

    std::string text = buffer;
    if (result.breakpoint_id >= 0)
        text += ", stopped by breakpoint " +
                std::to_string(result.breakpoint_id);

    return text;
}

json::value run_structured(const spectrum::run_result &result)
{
    json::value out = json::value::make_object();
    out.set("reason", json::value(
                          spectrum::stop_reason_name(result.reason)));
    out.set("tstates", json::value(result.tstates));
    out.set("instructions", json::value(result.instructions));
    out.set("frames", json::value(result.frames));
    out.set("pc", json::value(result.pc));
    if (result.breakpoint_id >= 0)
        out.set("breakpoint_id", json::value(result.breakpoint_id));
    return out;
}

//
// restart the machine.
//
class reset_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "reset"; }

    std::string description() const override
    {
        return "Reset the CPU and the ULA, restarting execution at "
               "address 0. RAM keeps its contents unless "
               "'clear_memory' is set, which is what real hardware "
               "does.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .boolean("clear_memory",
                     "Also wipe all 48K of RAM. Default false.")
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const bool clear = arguments["clear_memory"].as_bool(false);
        machine().reset(clear);

        return mcp::tool_result::of(
            clear ? "machine reset and RAM cleared"
                  : "machine reset, RAM left untouched",
            machine_state());
    }
};

//
// run for a bounded amount of time.
//
class run_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "run"; }

    std::string description() const override
    {
        return "Run the machine until a limit is reached or a "
               "breakpoint fires. Give at most one of 'frames', "
               "'tstates' or 'instructions'; with none, one frame runs, "
               "which is a single 50Hz video frame of 69888 T-states. "
               "Breakpoints always stop the run.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("frames", "Number of video frames to run.", 1,
                     1000)
            .integer("tstates", "Number of CPU T-states to run.", 1,
                     static_cast<std::int64_t>(max_tstates_per_call))
            .integer("instructions", "Number of instructions to run.",
                     1, 100000000)
            .boolean("stop_on_halt",
                     "Stop as soon as the CPU executes HALT.")
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        spectrum::run_limits limits;

        const auto frames = read_int_in(arguments["frames"], 1, 1000);
        const auto tstates = read_int_in(
            arguments["tstates"], 1,
            static_cast<std::int64_t>(max_tstates_per_call));
        const auto instructions =
            read_int_in(arguments["instructions"], 1, 100000000);

        if (!arguments["frames"].is_null() && !frames)
            return mcp::tool_result::failure(
                "'frames' must be between 1 and 1000");
        if (!arguments["tstates"].is_null() && !tstates)
            return mcp::tool_result::failure(
                "'tstates' must be between 1 and " +
                std::to_string(max_tstates_per_call));
        if (!arguments["instructions"].is_null() && !instructions)
            return mcp::tool_result::failure(
                "'instructions' must be between 1 and 100000000");

        if (frames)
            limits.max_frames = static_cast<spectrum::u64>(*frames);
        if (tstates)
            limits.max_tstates = static_cast<spectrum::u64>(*tstates);
        if (instructions)
            limits.max_instructions =
                static_cast<spectrum::u64>(*instructions);

        limits.stop_on_halt = arguments["stop_on_halt"].as_bool(false);

        if (limits.max_frames == 0 && limits.max_tstates == 0 &&
            limits.max_instructions == 0)
            limits.max_frames = default_run_frames;

        // a run bounded only by frames or instructions still needs a
        // ceiling, or a wedged program would never come back.
        if (limits.max_tstates == 0)
            limits.max_tstates = max_tstates_per_call;

        const spectrum::run_result result = machine().run(limits);
        return mcp::tool_result::of(describe_run(result),
                                    run_structured(result));
    }
};

//
// run until the program counter reaches an address.
//
class run_until_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "run_until"; }

    std::string description() const override
    {
        return "Run until the program counter reaches 'address', a "
               "breakpoint fires, or the time limit runs out. The "
               "address is tested between instructions, so it must be "
               "an instruction boundary to be seen. Returns a reason of "
               "'address_reached' on success and 'completed' if the "
               "limit was hit first.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("address",
                     "Address to stop at, 0 to 65535.", 0, 0xffff)
            .integer("max_tstates",
                     "Give up after this many T-states. Default "
                     "3500000, one second of emulated time.",
                     1, static_cast<std::int64_t>(max_tstates_per_call))
            .required({"address"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto address = read_int_in(arguments["address"], 0, 0xffff);
        if (!address)
            return mcp::tool_result::failure(
                "'address' is required and must be between 0 and 65535");

        const auto budget = read_int_in(
            arguments["max_tstates"], 1,
            static_cast<std::int64_t>(max_tstates_per_call), 3500000);
        if (!budget)
            return mcp::tool_result::failure(
                "'max_tstates' must be between 1 and " +
                std::to_string(max_tstates_per_call));

        const spectrum::run_result result = machine().run_until(
            static_cast<spectrum::u16>(*address),
            static_cast<spectrum::u64>(*budget));

        std::string text = describe_run(result);
        if (result.reason == spectrum::stop_reason::completed)
            text += "; the address was not reached within the limit";

        return mcp::tool_result::of(std::move(text),
                                    run_structured(result));
    }
};

//
// execute a few instructions.
//
class step_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "step"; }

    std::string description() const override
    {
        return "Execute a fixed number of instructions, one by "
               "default, and report where the CPU ended up. Use "
               "'registers' afterwards for the full register file.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("count", "Instructions to execute. Default 1.", 1,
                     1000000)
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto count = read_int_in(arguments["count"], 1, 1000000, 1);
        if (!count)
            return mcp::tool_result::failure(
                "'count' must be between 1 and 1000000");

        spectrum::run_limits limits;
        limits.max_instructions = static_cast<spectrum::u64>(*count);
        limits.max_tstates = max_tstates_per_call;

        const spectrum::run_result result = machine().run(limits);

        json::value structured = run_structured(result);
        const spectrum::cpu_registers regs = machine().registers();
        structured.set("af", json::value(regs.af));
        structured.set("bc", json::value(regs.bc));
        structured.set("de", json::value(regs.de));
        structured.set("hl", json::value(regs.hl));
        structured.set("sp", json::value(regs.sp));

        return mcp::tool_result::of(describe_run(result),
                                    std::move(structured));
    }
};

//
// report how the machine is configured and where it has got to.
//
class status_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "status"; }

    std::string description() const override
    {
        return "Report the emulated machine's configuration and "
               "current position: clock speed, frame geometry, the "
               "contention model in use, whether a ROM is loaded, and "
               "how much emulated time has passed. When Interface 1 "
               "serial is enabled, its TCP and modem state is included.";
    }

    json::value input_schema() const override
    {
        return schema_builder().build();
    }

    mcp::tool_result invoke(const json::value &) override
    {
        const spectrum::machine_timing &timing = machine().timing();

        json::value structured = machine_state();
        structured.set("cpu_hz", json::value(timing.cpu_hz));
        structured.set("tstates_per_frame",
                       json::value(timing.tstates_per_frame()));
        structured.set("tstates_per_line",
                       json::value(timing.tstates_per_line));
        structured.set("lines_per_frame",
                       json::value(timing.lines_per_frame));
        structured.set("screen_width",
                       json::value(timing.screen_width()));
        structured.set("screen_height",
                       json::value(timing.screen_height()));
        structured.set("contention",
                       json::value(machine().contention().name()));
        structured.set("custom_rom",
                       json::value(machine().mem().has_custom_rom()));
        structured.set("border",
                       json::value(machine().video().border()));
        structured.set("breakpoints",
                       json::value(static_cast<std::int64_t>(
                           machine().breakpoints().all().size())));
        structured.set("interface1_serial",
                       json::value(machine().interface1_serial_enabled()));

        const spectrum::tape_status tape = machine().tape().status();
        json::value tape_state = json::value::make_object();
        tape_state.set("loaded", json::value(tape.loaded));
        tape_state.set("playing", json::value(tape.playing));
        tape_state.set("finished", json::value(tape.finished));
        tape_state.set("stopped_by_command",
                       json::value(tape.stopped_by_command));
        tape_state.set("ear_level", json::value(tape.ear_level));
        tape_state.set("format", json::value(tape.format));
        tape_state.set("title", json::value(tape.title));
        tape_state.set("image_bytes", json::value(
                                           static_cast<std::int64_t>(
                                               tape.image_bytes)));
        tape_state.set("blocks", json::value(
                                      static_cast<std::int64_t>(tape.blocks)));
        tape_state.set("data_blocks",
                       json::value(static_cast<std::int64_t>(
                           tape.data_blocks)));
        tape_state.set("segments", json::value(
                                        static_cast<std::int64_t>(
                                            tape.segments)));
        tape_state.set("segment", json::value(
                                       static_cast<std::int64_t>(
                                           tape.segment)));
        tape_state.set("duration_tstates",
                       json::value(tape.duration_tstates));
        tape_state.set("position_tstates",
                       json::value(tape.position_tstates));
        structured.set("tape", std::move(tape_state));

        if (machine().interface1_serial_enabled()) {
            const spectrum::interface1_serial_status serial =
                machine().serial().status();
            json::value serial_state = json::value::make_object();
            serial_state.set("shadow_rom_loaded",
                             json::value(serial.shadow_rom_loaded));
            serial_state.set("shadow_rom_paged",
                             json::value(serial.shadow_rom_paged));
            serial_state.set("bridge_running",
                             json::value(serial.bridge_running));
            serial_state.set("data_port", json::value(serial.data_port));
            serial_state.set("control_port",
                             json::value(serial.control_port));
            serial_state.set("data_connected",
                             json::value(serial.data_connected));
            serial_state.set("control_connected",
                             json::value(serial.control_connected));
            serial_state.set("cts", json::value(serial.cts_input));
            serial_state.set("dcd", json::value(serial.dcd_input));
            serial_state.set("rts", json::value(serial.rts_output));
            serial_state.set("dtr", json::value(serial.dtr_output));
            serial_state.set("pending_rx_bytes",
                             json::value(static_cast<std::int64_t>(
                                 serial.pending_rx_bytes)));
            serial_state.set("pending_tx_bytes",
                             json::value(static_cast<std::int64_t>(
                                 serial.pending_tx_bytes)));
            serial_state.set("rx_bytes", json::value(serial.rx_bytes));
            serial_state.set("tx_bytes", json::value(serial.tx_bytes));
            structured.set("serial", std::move(serial_state));
        }

        char buffer[512];
        std::snprintf(
            buffer, sizeof buffer,
            "ZX Spectrum 48K at %d Hz, %d T-states per frame "
            "(%dx%d lines), contention model '%s'. %s. "
            "Elapsed %llu T-states over %llu frames; pc is %s.",
            timing.cpu_hz, timing.tstates_per_frame(),
            timing.tstates_per_line, timing.lines_per_frame,
            machine().contention().name(),
            machine().mem().has_custom_rom()
                ? "A ROM image is loaded"
                : "No ROM loaded, running the built-in stub",
            static_cast<unsigned long long>(machine().total_tstates()),
            static_cast<unsigned long long>(machine().frame_number()),
            hex16(machine().instruction_address()).c_str());

        std::string text = buffer;
        if (tape.loaded) {
            text += " A " + tape.format + " tape with " +
                    std::to_string(tape.blocks) + " blocks is " +
                    (tape.playing ? "playing." : "stopped.");
        }
        if (machine().interface1_serial_enabled()) {
            const spectrum::interface1_serial_status serial =
                machine().serial().status();
            text += " Interface 1 serial is enabled";
            text += serial.shadow_rom_loaded
                        ? " with its shadow ROM"
                        : " without a shadow ROM";
            if (serial.bridge_running) {
                text += " on TCP " + std::to_string(serial.data_port) +
                        "/" + std::to_string(serial.control_port);
                text += serial.data_connected
                            ? "; data client connected."
                            : "; no data client connected.";
            } else {
                text += " without a TCP bridge.";
            }
        }

        return mcp::tool_result::of(std::move(text),
                                    std::move(structured));
    }
};

} // namespace

void register_machine_tools(mcp::tool_registry &registry,
                            spectrum::machine &target)
{
    registry.add(std::make_unique<reset_tool>(target));
    registry.add(std::make_unique<run_tool>(target));
    registry.add(std::make_unique<run_until_tool>(target));
    registry.add(std::make_unique<step_tool>(target));
    registry.add(std::make_unique<status_tool>(target));
}

} // namespace tools
