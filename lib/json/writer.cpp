//
// implementation of the json serialiser.
//
// output is built into a single string with reserve-as-you-go growth.
// the only subtle part is number formatting: doubles are printed with
// the shortest representation that round trips, so a value written and
// read back compares equal.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "json/writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace json {

namespace {

constexpr char hex_digits[] = "0123456789abcdef";

//
// append text as a quoted json string literal.
//
void write_quoted(std::string &out, std::string_view text)
{
    out.push_back('"');
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                // control characters have no short escape and must not
                // appear raw, least of all in a line delimited stream.
                out += "\\u00";
                out.push_back(hex_digits[(c >> 4) & 0x0f]);
                out.push_back(hex_digits[c & 0x0f]);
            } else {
                out.push_back(raw);
            }
            break;
        }
    }
    out.push_back('"');
}

//
// append a double using the shortest form that reads back identically.
//
void write_double(std::string &out, double v)
{
    // json has no way to spell infinity or nan, so clamp them to null.
    if (!std::isfinite(v)) {
        out += "null";
        return;
    }

    std::array<char, 40> buffer{};
    for (int precision = 15; precision <= 17; ++precision) {
        const int written = std::snprintf(buffer.data(), buffer.size(),
                                          "%.*g", precision, v);
        if (written <= 0)
            break;
        if (std::strtod(buffer.data(), nullptr) == v)
            break;
    }
    out += buffer.data();
}

//
// recursive emitter. indent < 0 selects the compact layout.
//
void emit(std::string &out, const value &doc, int indent, int level)
{
    const bool pretty = indent >= 0;
    const auto newline_indent = [&](int depth) {
        if (!pretty)
            return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * depth), ' ');
    };

    switch (doc.type()) {
    case kind::null:
        out += "null";
        break;

    case kind::boolean:
        out += doc.as_bool() ? "true" : "false";
        break;

    case kind::integer:
        out += std::to_string(doc.as_int());
        break;

    case kind::real:
        write_double(out, doc.as_double());
        break;

    case kind::string:
        write_quoted(out, doc.as_string());
        break;

    case kind::array: {
        const array &items = doc.elements();
        if (items.empty()) {
            out += "[]";
            break;
        }
        out.push_back('[');
        bool first = true;
        for (const value &item : items) {
            if (!first)
                out.push_back(',');
            first = false;
            newline_indent(level + 1);
            emit(out, item, indent, level + 1);
        }
        newline_indent(level);
        out.push_back(']');
        break;
    }

    case kind::object: {
        const object &fields = doc.members();
        if (fields.empty()) {
            out += "{}";
            break;
        }
        out.push_back('{');
        bool first = true;
        for (const member &field : fields) {
            if (!first)
                out.push_back(',');
            first = false;
            newline_indent(level + 1);
            write_quoted(out, field.first);
            out.push_back(':');
            if (pretty)
                out.push_back(' ');
            emit(out, field.second, indent, level + 1);
        }
        newline_indent(level);
        out.push_back('}');
        break;
    }
    }
}

} // namespace

std::string write(const value &doc)
{
    std::string out;
    out.reserve(256);
    emit(out, doc, -1, 0);
    return out;
}

std::string write_pretty(const value &doc, int indent)
{
    std::string out;
    out.reserve(512);
    emit(out, doc, indent, 0);
    return out;
}

std::string quote(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    write_quoted(out, text);
    return out;
}

} // namespace json
