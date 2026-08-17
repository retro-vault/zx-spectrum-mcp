//
// tools for inspecting the CPU and managing breakpoints.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <cstdio>
#include <memory>

#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

//
// decode the flags byte into the conventional letters, upper case when
// set. F is the register software actually branches on, so spelling it
// out saves the reader decoding a hex value by hand.
//
std::string describe_flags(spectrum::u8 flags)
{
    static constexpr char names[8] = {'S', 'Z', 'Y', 'H',
                                      'X', 'P', 'N', 'C'};
    std::string out;

    for (int bit = 7; bit >= 0; --bit) {
        const bool set = (flags & (1u << bit)) != 0;
        const char letter = names[7 - bit];
        out.push_back(set ? letter
                          : static_cast<char>(letter - 'A' + 'a'));
    }

    return out;
}

//
// read and optionally modify the register file.
//
class registers_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "registers"; }

    std::string description() const override
    {
        return "Read the Z80 register file, and optionally change it. "
               "Any register named in the arguments is written before "
               "the state is reported, so this both inspects and pokes. "
               "'pc' is the address of the instruction about to "
               "execute. Flags are also given as letters, upper case "
               "when set, in the order SZYHXPNC.";
    }

    json::value input_schema() const override
    {
        schema_builder builder;
        builder.integer("af", "Set AF.", 0, 0xffff)
            .integer("bc", "Set BC.", 0, 0xffff)
            .integer("de", "Set DE.", 0, 0xffff)
            .integer("hl", "Set HL.", 0, 0xffff)
            .integer("ix", "Set IX.", 0, 0xffff)
            .integer("iy", "Set IY.", 0, 0xffff)
            .integer("sp", "Set the stack pointer.", 0, 0xffff)
            .integer("pc", "Set the program counter.", 0, 0xffff)
            .integer("af_alt", "Set the shadow AF.", 0, 0xffff)
            .integer("bc_alt", "Set the shadow BC.", 0, 0xffff)
            .integer("de_alt", "Set the shadow DE.", 0, 0xffff)
            .integer("hl_alt", "Set the shadow HL.", 0, 0xffff)
            .integer("i", "Set the interrupt vector register.", 0, 0xff)
            .integer("r", "Set the refresh register.", 0, 0xff)
            .integer("im", "Set the interrupt mode.", 0, 2)
            .boolean("iff1", "Set the interrupt enable flip flop.")
            .boolean("iff2", "Set the saved interrupt enable flip "
                             "flop.");
        return builder.build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        spectrum::cpu_registers regs = machine().registers();
        bool modified = false;

        const auto apply16 = [&](const char *field, spectrum::u16 &slot) {
            const auto value = read_int_in(arguments[field], 0, 0xffff);
            if (value && !arguments[field].is_null()) {
                slot = static_cast<spectrum::u16>(*value);
                modified = true;
            }
        };

        const auto apply8 = [&](const char *field, spectrum::u8 &slot,
                                std::int64_t high) {
            const auto value = read_int_in(arguments[field], 0, high);
            if (value && !arguments[field].is_null()) {
                slot = static_cast<spectrum::u8>(*value);
                modified = true;
            }
        };

        apply16("af", regs.af);
        apply16("bc", regs.bc);
        apply16("de", regs.de);
        apply16("hl", regs.hl);
        apply16("ix", regs.ix);
        apply16("iy", regs.iy);
        apply16("sp", regs.sp);
        apply16("pc", regs.pc);
        apply16("af_alt", regs.af_alt);
        apply16("bc_alt", regs.bc_alt);
        apply16("de_alt", regs.de_alt);
        apply16("hl_alt", regs.hl_alt);
        apply8("i", regs.i, 0xff);
        apply8("r", regs.r, 0xff);
        apply8("im", regs.im, 2);

        if (!arguments["iff1"].is_null()) {
            regs.iff1 = arguments["iff1"].as_bool(regs.iff1);
            modified = true;
        }
        if (!arguments["iff2"].is_null()) {
            regs.iff2 = arguments["iff2"].as_bool(regs.iff2);
            modified = true;
        }

        if (modified)
            machine().processor().set_registers(regs);

        const spectrum::cpu_registers now =
            machine().processor().registers();

        // after a write the core's own pc is authoritative, because
        // set_registers restarted the decoder there. otherwise the raw
        // pc has been advanced past the pending opcode by the
        // overlapped fetch, and the machine's tracked address is the
        // one a caller means.
        const spectrum::u16 reported_pc =
            modified ? now.pc : machine().instruction_address();

        const auto flags = static_cast<spectrum::u8>(now.af & 0xff);
        const auto accumulator =
            static_cast<spectrum::u8>((now.af >> 8) & 0xff);

        json::value structured = json::value::make_object();
        structured.set("af", json::value(now.af));
        structured.set("bc", json::value(now.bc));
        structured.set("de", json::value(now.de));
        structured.set("hl", json::value(now.hl));
        structured.set("ix", json::value(now.ix));
        structured.set("iy", json::value(now.iy));
        structured.set("sp", json::value(now.sp));
        structured.set("pc", json::value(reported_pc));
        structured.set("wz", json::value(now.wz));
        structured.set("af_alt", json::value(now.af_alt));
        structured.set("bc_alt", json::value(now.bc_alt));
        structured.set("de_alt", json::value(now.de_alt));
        structured.set("hl_alt", json::value(now.hl_alt));
        structured.set("i", json::value(now.i));
        structured.set("r", json::value(now.r));
        structured.set("im", json::value(now.im));
        structured.set("iff1", json::value(now.iff1));
        structured.set("iff2", json::value(now.iff2));
        structured.set("halted", json::value(now.halted));
        structured.set("flags", json::value(describe_flags(flags)));
        structured.set("tstates", json::value(machine().total_tstates()));

        char buffer[512];
        std::snprintf(
            buffer, sizeof buffer,
            "PC=%04X SP=%04X  AF=%04X BC=%04X DE=%04X HL=%04X\n"
            "IX=%04X IY=%04X  AF'=%04X BC'=%04X DE'=%04X HL'=%04X\n"
            "A=%02X F=%s  I=%02X R=%02X IM=%u IFF1=%d IFF2=%d%s",
            reported_pc, now.sp,
            now.af, now.bc, now.de, now.hl, now.ix, now.iy, now.af_alt,
            now.bc_alt, now.de_alt, now.hl_alt, accumulator,
            describe_flags(flags).c_str(), now.i, now.r,
            static_cast<unsigned>(now.im), now.iff1 ? 1 : 0,
            now.iff2 ? 1 : 0, now.halted ? "  [HALTED]" : "");

        return mcp::tool_result::of(buffer, std::move(structured));
    }
};

//
// add, remove and list breakpoints.
//
class breakpoint_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "breakpoint"; }

    std::string description() const override
    {
        return "Manage breakpoints. Action 'add' arms one and returns "
               "its id; 'remove' takes an id; 'list' shows all with "
               "their hit counts; 'clear' removes every one; 'enable' "
               "and 'disable' take an id. Kinds are execute, "
               "memory_read, memory_write, io_read and io_write. "
               "Execute breakpoints stop before the instruction runs; "
               "the others stop at the end of the instruction that "
               "made the access.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("action", "What to do. Default 'list'.",
                    {"add", "remove", "list", "clear", "enable",
                     "disable"})
            .string("kind", "What to watch for, when adding.",
                    {"execute", "memory_read", "memory_write",
                     "io_read", "io_write"})
            .integer("address",
                     "Address or port to watch, when adding.", 0,
                     0xffff)
            .integer("value",
                     "Only fire when the byte transferred equals this. "
                     "Ignored for execute breakpoints.",
                     0, 0xff)
            .integer("id", "Which breakpoint to act on.", 1, 1000000)
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const std::string action =
            arguments["action"].as_string("list");
        spectrum::breakpoint_set &set = machine().breakpoints();

        if (action == "add")
            return add(arguments, set);

        if (action == "list")
            return list(set);

        if (action == "clear") {
            const std::size_t count = set.all().size();
            set.clear();
            return mcp::tool_result::text("removed " +
                                          std::to_string(count) +
                                          " breakpoints");
        }

        const auto id = read_int_in(arguments["id"], 1, 1000000);
        if (!id)
            return mcp::tool_result::failure(
                "'id' is required for action '" + action + "'");

        const auto target = static_cast<int>(*id);

        if (action == "remove") {
            if (!set.remove(target))
                return mcp::tool_result::failure(
                    "no breakpoint with id " + std::to_string(target));
            return mcp::tool_result::text("removed breakpoint " +
                                          std::to_string(target));
        }

        if (action == "enable" || action == "disable") {
            const bool enable = action == "enable";
            if (!set.set_enabled(target, enable))
                return mcp::tool_result::failure(
                    "no breakpoint with id " + std::to_string(target));
            return mcp::tool_result::text(
                (enable ? "enabled breakpoint " : "disabled breakpoint ") +
                std::to_string(target));
        }

        return mcp::tool_result::failure("unknown action '" + action +
                                         "'");
    }

private:
    mcp::tool_result add(const json::value &arguments,
                         spectrum::breakpoint_set &set)
    {
        const std::string kind_name =
            arguments["kind"].as_string("execute");
        const auto kind =
            spectrum::breakpoint_kind_from_name(kind_name);
        if (!kind)
            return mcp::tool_result::failure("unknown kind '" +
                                             kind_name + "'");

        const auto address = read_int_in(arguments["address"], 0, 0xffff);
        if (!address)
            return mcp::tool_result::failure(
                "'address' is required and must be between 0 and 65535");

        std::optional<spectrum::u8> value;
        if (!arguments["value"].is_null()) {
            const auto raw = read_int_in(arguments["value"], 0, 0xff);
            if (!raw)
                return mcp::tool_result::failure(
                    "'value' must be between 0 and 255");
            value = static_cast<spectrum::u8>(*raw);
        }

        const int id = set.add(*kind, static_cast<spectrum::u16>(*address),
                               value);

        json::value structured = json::value::make_object();
        structured.set("id", json::value(id));
        structured.set("kind", json::value(kind_name));
        structured.set("address", json::value(*address));

        std::string text = "breakpoint " + std::to_string(id) + ": " +
                           kind_name + " at " +
                           hex16(static_cast<unsigned>(*address));
        if (value)
            text += " when the byte is " + hex8(*value);

        return mcp::tool_result::of(std::move(text),
                                    std::move(structured));
    }

    mcp::tool_result list(const spectrum::breakpoint_set &set)
    {
        const std::vector<spectrum::breakpoint> &items = set.all();

        if (items.empty())
            return mcp::tool_result::of(
                "no breakpoints are set",
                json::value::make_array());

        json::value entries = json::value::make_array();
        std::string text;

        for (const spectrum::breakpoint &item : items) {
            json::value entry = json::value::make_object();
            entry.set("id", json::value(item.id));
            entry.set("kind", json::value(spectrum::breakpoint_kind_name(
                                  item.kind)));
            entry.set("address", json::value(item.address));
            entry.set("enabled", json::value(item.enabled));
            entry.set("hits", json::value(item.hits));
            if (item.value)
                entry.set("value", json::value(*item.value));
            entries.push_back(std::move(entry));

            if (!text.empty())
                text += '\n';
            text += std::to_string(item.id) + ": " +
                    spectrum::breakpoint_kind_name(item.kind) + " at " +
                    hex16(item.address) +
                    (item.enabled ? "" : " (disabled)") + ", " +
                    std::to_string(item.hits) + " hits";
        }

        return mcp::tool_result::of(std::move(text), std::move(entries));
    }
};

} // namespace

void register_cpu_tools(mcp::tool_registry &registry,
                        spectrum::machine &target)
{
    registry.add(std::make_unique<registers_tool>(target));
    registry.add(std::make_unique<breakpoint_tool>(target));
}

} // namespace tools
