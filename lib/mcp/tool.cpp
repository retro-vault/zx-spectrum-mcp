//
// implementation of content blocks, tool results and base64.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/tool.h"

namespace mcp {

namespace {

constexpr char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

content_block content_block::text(std::string body)
{
    content_block block;
    block.type_ = "text";
    block.body_ = std::move(body);
    return block;
}

content_block content_block::image(std::string base64,
                                   std::string mime_type)
{
    content_block block;
    block.type_ = "image";
    block.body_ = std::move(base64);
    block.mime_type_ = std::move(mime_type);
    return block;
}

json::value content_block::to_json() const
{
    json::value block = json::value::make_object();
    block.set("type", json::value(type_));

    if (type_ == "image") {
        block.set("data", json::value(body_));
        block.set("mimeType", json::value(mime_type_));
    } else {
        block.set("text", json::value(body_));
    }

    return block;
}

tool_result tool_result::text(std::string body)
{
    tool_result result;
    result.content.push_back(content_block::text(std::move(body)));
    return result;
}

tool_result tool_result::of(std::string body, json::value structured)
{
    tool_result result;
    result.content.push_back(content_block::text(std::move(body)));
    result.structured = std::move(structured);
    return result;
}

tool_result tool_result::failure(std::string message)
{
    tool_result result;
    result.content.push_back(content_block::text(std::move(message)));
    result.is_error = true;
    return result;
}

json::value tool_result::to_json() const
{
    json::value blocks = json::value::make_array();
    for (const content_block &block : content)
        blocks.push_back(block.to_json());

    json::value result = json::value::make_object();
    result.set("content", std::move(blocks));

    if (!structured.is_null())
        result.set("structuredContent", structured);

    // the field is only meaningful when true, and clients treat its
    // absence as success.
    if (is_error)
        result.set("isError", json::value(true));

    return result;
}

json::value tool::describe() const
{
    json::value entry = json::value::make_object();
    entry.set("name", json::value(name()));
    entry.set("description", json::value(description()));
    entry.set("inputSchema", input_schema());
    return entry;
}

std::string base64_encode(std::string_view data)
{
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const auto a = static_cast<unsigned char>(data[i]);
        const auto b = static_cast<unsigned char>(data[i + 1]);
        const auto c = static_cast<unsigned char>(data[i + 2]);
        const unsigned triple = (a << 16) | (b << 8) | c;

        out.push_back(base64_alphabet[(triple >> 18) & 0x3f]);
        out.push_back(base64_alphabet[(triple >> 12) & 0x3f]);
        out.push_back(base64_alphabet[(triple >> 6) & 0x3f]);
        out.push_back(base64_alphabet[triple & 0x3f]);
        i += 3;
    }

    // the tail is one or two bytes, padded out to a full quad.
    const std::size_t remaining = data.size() - i;
    if (remaining == 1) {
        const auto a = static_cast<unsigned char>(data[i]);
        const unsigned triple = a << 16;
        out.push_back(base64_alphabet[(triple >> 18) & 0x3f]);
        out.push_back(base64_alphabet[(triple >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const auto a = static_cast<unsigned char>(data[i]);
        const auto b = static_cast<unsigned char>(data[i + 1]);
        const unsigned triple = (a << 16) | (b << 8);
        out.push_back(base64_alphabet[(triple >> 18) & 0x3f]);
        out.push_back(base64_alphabet[(triple >> 12) & 0x3f]);
        out.push_back(base64_alphabet[(triple >> 6) & 0x3f]);
        out.push_back('=');
    }

    return out;
}

} // namespace mcp
