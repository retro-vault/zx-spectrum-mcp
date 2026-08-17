//
// tools for reading and writing the address space.
//
// both work through the debugger view of memory rather than the CPU
// view, so a read has no side effects and a write can be told to patch
// the ROM. neither costs the machine any emulated time.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <memory>

#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

//
// how much can be read in one call.
//
// the result travels as a hex dump inside a JSON message, so a whole
// 64K read would produce a response far larger than it is useful. a
// caller who wants everything can page through it.
//
constexpr std::int64_t max_read_length = 4096;

//
// read a block of memory.
//
class read_memory_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "read_memory"; }

    std::string description() const override
    {
        return "Read bytes from the address space and return them as a "
               "hex dump plus a byte array. Reading has no side "
               "effects and consumes no emulated time. Addresses wrap "
               "at 65535. Useful landmarks: 0x4000 display file, "
               "0x5800 attributes, 0x5C00 system variables.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("address",
                     "First address to read, 0 to 65535. Hex strings "
                     "such as '0x5C00' are accepted.",
                     0, 0xffff)
            .integer("length",
                     "How many bytes to read. Default 16, maximum 4096.",
                     1, max_read_length)
            .required({"address"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto address = read_int_in(arguments["address"], 0, 0xffff);
        if (!address)
            return mcp::tool_result::failure(
                "'address' is required and must be between 0 and 65535");

        const auto length =
            read_int_in(arguments["length"], 1, max_read_length, 16);
        if (!length)
            return mcp::tool_result::failure(
                "'length' must be between 1 and " +
                std::to_string(max_read_length));

        const auto start = static_cast<spectrum::u16>(*address);
        const std::vector<spectrum::u8> data = machine().mem().read_block(
            start, static_cast<std::size_t>(*length));

        json::value bytes = json::value::make_array();
        for (const spectrum::u8 byte : data)
            bytes.push_back(json::value(byte));

        json::value structured = json::value::make_object();
        structured.set("address", json::value(start));
        structured.set("length", json::value(*length));
        structured.set("bytes", std::move(bytes));

        return mcp::tool_result::of(hex_dump(start, data),
                                    std::move(structured));
    }
};

//
// write a block of memory.
//
class write_memory_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "write_memory"; }

    std::string description() const override
    {
        return "Write bytes into the address space. By default writes "
               "below 0x4000 are skipped, because that is ROM and the "
               "CPU cannot write there either; set 'allow_rom' to "
               "patch it anyway. Consumes no emulated time.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .integer("address", "First address to write, 0 to 65535.",
                     0, 0xffff)
            .string("data",
                    "Bytes to write, either as hex digit pairs like "
                    "'3e00c9' or as a JSON array of numbers.")
            .boolean("allow_rom",
                     "Permit writes below 0x4000. Default false.")
            .required({"address", "data"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const auto address = read_int_in(arguments["address"], 0, 0xffff);
        if (!address)
            return mcp::tool_result::failure(
                "'address' is required and must be between 0 and 65535");

        const auto data = read_bytes(arguments["data"]);
        if (!data)
            return mcp::tool_result::failure(
                "'data' is required and must be hex digit pairs or an "
                "array of byte values 0 to 255");

        if (data->empty())
            return mcp::tool_result::failure("'data' is empty");

        const bool allow_rom = arguments["allow_rom"].as_bool(false);
        const auto start = static_cast<spectrum::u16>(*address);

        const std::size_t written =
            machine().mem().write_block(start, *data, allow_rom);

        json::value structured = json::value::make_object();
        structured.set("address", json::value(start));
        structured.set("requested", json::value(static_cast<std::int64_t>(
                                        data->size())));
        structured.set("written", json::value(static_cast<std::int64_t>(
                                      written)));

        std::string text = "wrote " + std::to_string(written) +
                           " of " + std::to_string(data->size()) +
                           " bytes at " + hex16(start);

        if (written < data->size())
            text += "; the rest fell in ROM and were skipped, pass "
                    "allow_rom to force them";

        return mcp::tool_result::of(std::move(text),
                                    std::move(structured));
    }
};

} // namespace

void register_memory_tools(mcp::tool_registry &registry,
                           spectrum::machine &target)
{
    registry.add(std::make_unique<read_memory_tool>(target));
    registry.add(std::make_unique<write_memory_tool>(target));
}

} // namespace tools
