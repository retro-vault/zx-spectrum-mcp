//
// implementation of the shared tool scaffolding.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "tools/support.h"

#include <cctype>
#include <charconv>
#include <cstdio>

namespace tools {

namespace {

//
// trim ascii whitespace from both ends.
//
std::string_view trim(std::string_view text)
{
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

//
// parse a number written in decimal or in one of the three hex
// conventions this machine's documentation uses.
//
std::optional<std::int64_t> parse_number(std::string_view text)
{
    text = trim(text);
    if (text.empty())
        return std::nullopt;

    bool negative = false;
    if (text.front() == '-') {
        negative = true;
        text.remove_prefix(1);
    } else if (text.front() == '+') {
        text.remove_prefix(1);
    }

    int base = 10;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    } else if (!text.empty() && (text.front() == '$' ||
                                 text.front() == '#')) {
        base = 16;
        text.remove_prefix(1);
    }

    if (text.empty())
        return std::nullopt;

    std::int64_t result = 0;
    const char *first = text.data();
    const char *last = first + text.size();
    const auto parsed = std::from_chars(first, last, result, base);

    if (parsed.ec != std::errc() || parsed.ptr != last)
        return std::nullopt;

    return negative ? -result : result;
}

} // namespace

schema_builder &schema_builder::integer(
    std::string name, std::string description,
    std::optional<std::int64_t> minimum,
    std::optional<std::int64_t> maximum)
{
    json::value property = json::value::make_object();
    property.set("type", json::value("integer"));
    property.set("description", json::value(std::move(description)));
    if (minimum)
        property.set("minimum", json::value(*minimum));
    if (maximum)
        property.set("maximum", json::value(*maximum));

    properties_.set(std::move(name), std::move(property));
    return *this;
}

schema_builder &schema_builder::string(std::string name,
                                       std::string description,
                                       std::vector<std::string> allowed)
{
    json::value property = json::value::make_object();
    property.set("type", json::value("string"));
    property.set("description", json::value(std::move(description)));

    if (!allowed.empty()) {
        json::value values = json::value::make_array();
        for (std::string &item : allowed)
            values.push_back(json::value(std::move(item)));
        property.set("enum", std::move(values));
    }

    properties_.set(std::move(name), std::move(property));
    return *this;
}

schema_builder &schema_builder::boolean(std::string name,
                                        std::string description)
{
    json::value property = json::value::make_object();
    property.set("type", json::value("boolean"));
    property.set("description", json::value(std::move(description)));

    properties_.set(std::move(name), std::move(property));
    return *this;
}

schema_builder &schema_builder::string_array(std::string name,
                                             std::string description)
{
    json::value items = json::value::make_object();
    items.set("type", json::value("string"));

    json::value property = json::value::make_object();
    property.set("type", json::value("array"));
    property.set("description", json::value(std::move(description)));
    property.set("items", std::move(items));

    properties_.set(std::move(name), std::move(property));
    return *this;
}

schema_builder &schema_builder::required(std::vector<std::string> names)
{
    for (std::string &name : names)
        required_.push_back(std::move(name));
    return *this;
}

json::value schema_builder::build() const
{
    json::value schema = json::value::make_object();
    schema.set("type", json::value("object"));
    schema.set("properties", properties_);

    if (!required_.empty()) {
        json::value names = json::value::make_array();
        for (const std::string &name : required_)
            names.push_back(json::value(name));
        schema.set("required", std::move(names));
    }

    return schema;
}

std::optional<std::int64_t> read_int(const json::value &value)
{
    if (value.is_integer())
        return value.as_int();

    if (value.is_number()) {
        const double raw = value.as_double();
        const auto rounded = static_cast<std::int64_t>(raw);
        if (static_cast<double>(rounded) == raw)
            return rounded;
        return std::nullopt;
    }

    if (value.is_string())
        return parse_number(value.as_string());

    return std::nullopt;
}

std::optional<std::int64_t> read_int_in(
    const json::value &value, std::int64_t low, std::int64_t high,
    std::optional<std::int64_t> fallback)
{
    if (value.is_null())
        return fallback;

    const std::optional<std::int64_t> parsed = read_int(value);
    if (!parsed)
        return std::nullopt;

    if (*parsed < low || *parsed > high)
        return std::nullopt;

    return parsed;
}

std::optional<std::vector<spectrum::u8>> read_bytes(
    const json::value &value)
{
    std::vector<spectrum::u8> bytes;

    if (value.is_array()) {
        for (const json::value &item : value.elements()) {
            const std::optional<std::int64_t> byte = read_int(item);
            if (!byte || *byte < 0 || *byte > 255)
                return std::nullopt;
            bytes.push_back(static_cast<spectrum::u8>(*byte));
        }
        return bytes;
    }

    if (value.is_string()) {
        // a run of hex digit pairs, with optional spacing between
        // them, which is how a listing or a poke table is written.
        std::string digits;
        for (const char c : value.as_string()) {
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
                continue;
            if (!std::isxdigit(static_cast<unsigned char>(c)))
                return std::nullopt;
            digits.push_back(c);
        }

        if (digits.size() % 2 != 0)
            return std::nullopt;

        for (std::size_t i = 0; i < digits.size(); i += 2) {
            const std::string pair = digits.substr(i, 2);
            bytes.push_back(static_cast<spectrum::u8>(
                std::stoul(pair, nullptr, 16)));
        }
        return bytes;
    }

    return std::nullopt;
}

std::string hex16(unsigned value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%04X", value & 0xffffu);
    return buffer;
}

std::string hex8(unsigned value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "0x%02X", value & 0xffu);
    return buffer;
}

std::string hex_dump(spectrum::u16 address,
                     const std::vector<spectrum::u8> &data)
{
    constexpr std::size_t per_line = 16;

    std::string out;
    char buffer[32];

    for (std::size_t offset = 0; offset < data.size();
         offset += per_line) {
        const std::size_t count =
            std::min(per_line, data.size() - offset);

        std::snprintf(buffer, sizeof buffer, "%04X: ",
                      static_cast<unsigned>((address + offset) & 0xffff));
        out += buffer;

        for (std::size_t i = 0; i < per_line; ++i) {
            if (i < count) {
                std::snprintf(buffer, sizeof buffer, "%02X ",
                              data[offset + i]);
                out += buffer;
            } else {
                out += "   ";
            }
        }

        // the ascii column, with anything unprintable shown as a dot.
        out += '|';
        for (std::size_t i = 0; i < count; ++i) {
            const spectrum::u8 byte = data[offset + i];
            out += (byte >= 0x20 && byte < 0x7f)
                       ? static_cast<char>(byte)
                       : '.';
        }
        out += '|';

        if (offset + per_line < data.size())
            out += '\n';
    }

    return out;
}

machine_tool::machine_tool(spectrum::machine &target) : machine_(target)
{
}

spectrum::machine &machine_tool::machine() const { return machine_; }

json::value machine_tool::machine_state() const
{
    json::value state = json::value::make_object();
    state.set("pc", json::value(machine_.instruction_address()));
    state.set("tstates", json::value(machine_.total_tstates()));
    state.set("frame", json::value(machine_.frame_number()));
    state.set("frame_tstate", json::value(machine_.frame_tstate()));
    state.set("halted",
              json::value(machine_.processor().halted()));
    return state;
}

} // namespace tools

