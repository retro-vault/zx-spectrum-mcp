//
// recursive descent json parser.
//
// accepts the grammar from rfc 8259 and nothing beyond it: no comments,
// no trailing commas, no unquoted keys. protocol input arrives from
// another program rather than from a human, so a strict reader that
// reports a byte offset is more useful than a forgiving one.
//
// parsing never throws and never aborts on malformed input. the result
// carries either a value or a diagnostic, because a json parse failure
// is a normal protocol event that must be answered with a jsonrpc parse
// error, not an exception that unwinds the server loop.
//
// nesting depth is capped to keep a hostile or truncated document from
// exhausting the stack through recursion.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <cstddef>
#include <string>
#include <string_view>

#include "json/value.h"

namespace json {

//
// largest object/array nesting depth accepted by the parser.
//
inline constexpr int max_parse_depth = 128;

//
// outcome of a parse attempt.
//
struct parse_result {
    // true when the document parsed cleanly.
    bool ok = false;

    // the parsed document; null when ok is false.
    value document;

    // human readable failure reason; empty when ok is true.
    std::string error;

    // byte offset within the input where parsing stopped.
    std::size_t offset = 0;

    explicit operator bool() const noexcept { return ok; }
};

//
// Parse a complete json document.
//
// Parameters:
//      text        - the document. must contain exactly one json value,
//                    optionally surrounded by whitespace.
//
// Returns:
//      a parse_result whose ok flag says whether document is usable.
//
// Notes:
//      trailing non-whitespace after the value is an error, so a
//      truncated or doubled message is rejected rather than silently
//      half accepted.
//
// Sample call:
//      auto r = json::parse(R"({"jsonrpc":"2.0"})");
//      if (r) use(r.document);
//
parse_result parse(std::string_view text);

} // namespace json

#endif // JSON_PARSER_H
