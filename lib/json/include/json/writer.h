//
// json serialiser.
//
// emits rfc 8259 text. two layouts are offered: compact, used on the
// wire because the mcp stdio transport is line delimited and a message
// must not contain a raw newline, and indented, used for log files and
// for the --dump diagnostics where a human has to read the result.
//
// integers are written without a decimal point and doubles with enough
// digits to round trip exactly, so a value survives serialise/parse
// unchanged.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef JSON_WRITER_H
#define JSON_WRITER_H

#include <string>

#include "json/value.h"

namespace json {

//
// Serialise a value to compact json with no superfluous whitespace.
//
// Parameters:
//      doc         - the document to serialise.
//
// Returns:
//      a single line of json text containing no raw control characters,
//      safe to send over the newline delimited stdio transport.
//
// Sample call:
//      std::string line = json::write(response);
//
std::string write(const value &doc);

//
// Serialise a value to indented, multi line json.
//
// Parameters:
//      doc         - the document to serialise.
//      indent      - spaces added per nesting level.
//
// Returns:
//      pretty printed json text. never send this over the transport;
//      it contains newlines.
//
std::string write_pretty(const value &doc, int indent = 2);

//
// Escape a string into a quoted json string literal.
//
// Parameters:
//      text        - raw text, may contain any byte values.
//
// Returns:
//      the quoted and escaped literal, including surrounding quotes.
//
// Notes:
//      control characters below 0x20 become \u escapes. invalid utf-8
//      bytes are replaced so that output is always valid json.
//
std::string quote(std::string_view text);

} // namespace json

#endif // JSON_WRITER_H
