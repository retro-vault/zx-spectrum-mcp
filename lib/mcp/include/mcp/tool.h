//
// a callable tool, in the command pattern sense.
//
// each tool is an object that knows its own name, describes its own
// arguments as a JSON Schema, and executes itself. the server never
// switches on a tool name: it looks the object up and invokes it. that
// is what keeps adding a tool to a single new class with no edits
// anywhere else.
//
// results are content blocks rather than plain strings because MCP lets
// one call return a mix of text and images, which is exactly what the
// screen tool needs: a PNG for the picture and a line of text saying
// what frame it came from.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_TOOL_H
#define MCP_TOOL_H

#include <string>
#include <string_view>
#include <vector>

#include "json/value.h"

namespace mcp {

//
// one block of a tool's answer.
//
class content_block {
public:
    //
    // Build a text block.
    //
    static content_block text(std::string body);

    //
    // Build an image block.
    //
    // Parameters:
    //      base64      - the image bytes, base64 encoded.
    //      mime_type   - for instance "image/png".
    //
    static content_block image(std::string base64, std::string mime_type);

    //
    // Returns: the block as MCP content JSON.
    //
    json::value to_json() const;

private:
    std::string type_;
    std::string body_;
    std::string mime_type_;
};

//
// what a tool call produced.
//
struct tool_result {
    std::vector<content_block> content;

    // true when the call failed in a way the model should see and can
    // act on. protocol level failures are reported as JSON-RPC errors
    // instead; this is for "that address is out of range".
    bool is_error = false;

    // optional machine readable mirror of the text content, returned
    // as structuredContent so a client can parse rather than scrape.
    json::value structured;

    //
    // Build a successful text only result.
    //
    static tool_result text(std::string body);

    //
    // Build a successful result carrying both prose and structured
    // data, which is how most tools here answer.
    //
    static tool_result of(std::string body, json::value structured);

    //
    // Build a failed result.
    //
    static tool_result failure(std::string message);

    //
    // Returns: the result as MCP tools/call result JSON.
    //
    json::value to_json() const;
};

//
// one tool the server offers.
//
class tool {
public:
    virtual ~tool() = default;

    //
    // Returns: the name clients call this tool by. must be unique.
    //
    virtual std::string name() const = 0;

    //
    // Returns: what the tool does, written for a model to read.
    //
    virtual std::string description() const = 0;

    //
    // Returns:
    //      a JSON Schema object describing the arguments. must be of
    //      type "object" even when the tool takes none.
    //
    virtual json::value input_schema() const = 0;

    //
    // Run the tool.
    //
    // Parameters:
    //      arguments   - the caller's arguments object, already
    //                    checked to be an object or null.
    //
    // Returns:
    //      the result, successful or not.
    //
    // Notes:
    //      must not throw. an implementation that cannot do what was
    //      asked returns tool_result::failure().
    //
    virtual tool_result invoke(const json::value &arguments) = 0;

    //
    // Returns:
    //      this tool as one entry of a tools/list reply.
    //
    json::value describe() const;
};

//
// Encode bytes as base64, for image content blocks.
//
// Parameters:
//      data        - the raw bytes.
//
// Returns:
//      standard base64 with padding and no line breaks.
//
std::string base64_encode(std::string_view data);

} // namespace mcp

#endif // MCP_TOOL_H
