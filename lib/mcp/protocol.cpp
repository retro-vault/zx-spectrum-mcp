//
// implementation of the JSON-RPC envelope.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/protocol.h"

namespace mcp {

bool request::is_notification() const { return id.is_null(); }

bool parse_request(const json::value &document, request &out,
                   std::string &error)
{
    if (!document.is_object()) {
        error = "a JSON-RPC message must be an object";
        return false;
    }

    const json::value &version = document["jsonrpc"];
    if (!version.is_string() || version.as_string() != jsonrpc_version) {
        error = "missing or unsupported \"jsonrpc\" version, expected "
                "\"2.0\"";
        return false;
    }

    const json::value &method = document["method"];
    if (!method.is_string() || method.as_string().empty()) {
        error = "missing or empty \"method\"";
        return false;
    }

    // an id may be a string, a number, or absent. anything else is not
    // a legal JSON-RPC id.
    const json::value &id = document["id"];
    if (!id.is_null() && !id.is_string() && !id.is_number()) {
        error = "\"id\" must be a string or a number when present";
        return false;
    }

    out.id = id;
    out.method = method.as_string();
    out.params = document["params"];
    return true;
}

json::value make_response(const json::value &id, json::value result)
{
    json::value response = json::value::make_object();
    response.set("jsonrpc", json::value(jsonrpc_version));
    response.set("id", id);
    response.set("result", std::move(result));
    return response;
}

json::value make_error(const json::value &id, int code,
                       const std::string &message, json::value data)
{
    json::value error = json::value::make_object();
    error.set("code", json::value(code));
    error.set("message", json::value(message));
    if (!data.is_null())
        error.set("data", std::move(data));

    json::value response = json::value::make_object();
    response.set("jsonrpc", json::value(jsonrpc_version));
    response.set("id", id);
    response.set("error", std::move(error));
    return response;
}

} // namespace mcp
