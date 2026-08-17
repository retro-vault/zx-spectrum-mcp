//
// shared scaffolding for the emulator tools.
//
// two jobs. first, a builder for JSON Schema, because every tool has to
// describe its arguments and writing that out by hand would bury the
// interesting part of each tool in boilerplate. second, argument
// readers that accept what a person or a model would naturally write.
//
// the argument readers take hex as well as decimal. addresses, ports
// and opcodes are conventionally written in hex for this machine, and a
// tool that only accepted 16384 when the caller means 0x4000 would be
// tiresome to use.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef TOOLS_SUPPORT_H
#define TOOLS_SUPPORT_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json/value.h"
#include "mcp/tool.h"
#include "spectrum/machine.h"

namespace tools {

//
// assembles a JSON Schema object describing a tool's arguments.
//
// every method returns *this so a schema reads as one expression.
//
class schema_builder {
public:
    //
    // Add an integer property.
    //
    // Parameters:
    //      name        - property name.
    //      description - what it means, written for a model.
    //      minimum     - inclusive lower bound, if any.
    //      maximum     - inclusive upper bound, if any.
    //
    schema_builder &integer(std::string name, std::string description,
                            std::optional<std::int64_t> minimum = {},
                            std::optional<std::int64_t> maximum = {});

    //
    // Add a string property, optionally restricted to a fixed set.
    //
    schema_builder &string(std::string name, std::string description,
                           std::vector<std::string> allowed = {});

    //
    // Add a boolean property.
    //
    schema_builder &boolean(std::string name, std::string description);

    //
    // Add an array of strings property.
    //
    schema_builder &string_array(std::string name,
                                 std::string description);

    //
    // Mark previously added properties as required.
    //
    schema_builder &required(std::vector<std::string> names);

    //
    // Returns: the finished schema object.
    //
    json::value build() const;

private:
    json::value properties_ = json::value::make_object();
    std::vector<std::string> required_;
};

//
// Read an integer argument.
//
// Parameters:
//      value       - the raw argument.
//
// Returns:
//      the number, or nullopt when it is absent or unreadable.
//
// Notes:
//      accepts a JSON number, or a string in decimal or in hex written
//      as 0x1234, $1234 or #1234. leading and trailing spaces are
//      ignored.
//
std::optional<std::int64_t> read_int(const json::value &value);

//
// Read an integer argument that must fall in a range.
//
// Parameters:
//      value       - the raw argument.
//      low, high   - inclusive bounds.
//      fallback    - returned when the argument is absent.
//
// Returns:
//      the number, or nullopt when present but out of range or
//      unreadable.
//
std::optional<std::int64_t> read_int_in(const json::value &value,
                                        std::int64_t low,
                                        std::int64_t high,
                                        std::optional<std::int64_t>
                                            fallback = {});

//
// Read a list of bytes.
//
// Parameters:
//      value       - either an array of integers, or a string of hex
//                    digit pairs such as "3e00c9" or "3E 00 C9".
//
// Returns:
//      the bytes, or nullopt when the value is neither form or holds
//      something outside 0..255.
//
std::optional<std::vector<spectrum::u8>> read_bytes(
    const json::value &value);

//
// Format a byte range as an annotated hex dump.
//
// Parameters:
//      address     - address of the first byte.
//      data        - the bytes.
//
// Returns:
//      lines of "ADDR: xx xx ... |ascii|", as a debugger prints them.
//
std::string hex_dump(spectrum::u16 address,
                     const std::vector<spectrum::u8> &data);

//
// Format a 16 bit value as 0xNNNN.
//
std::string hex16(unsigned value);

//
// Format an 8 bit value as 0xNN.
//
std::string hex8(unsigned value);

//
// base class for every tool that drives the emulator.
//
// holds the machine reference so the concrete tools do not each repeat
// it, and gives them one place to reach it from.
//
class machine_tool : public mcp::tool {
public:
    explicit machine_tool(spectrum::machine &target);

protected:
    spectrum::machine &machine() const;

    //
    // Build the block of fields every tool appends to its structured
    // result: where the CPU is and how much time has passed.
    //
    json::value machine_state() const;

private:
    spectrum::machine &machine_;
};

} // namespace tools

#endif // TOOLS_SUPPORT_H
