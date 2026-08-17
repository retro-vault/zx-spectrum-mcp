//
// JSON-RPC 2.0 and Model Context Protocol vocabulary.
//
// MCP is JSON-RPC 2.0 with a fixed set of methods. this header holds
// the constants, the parsed shape of an incoming message, and the two
// builders every handler uses to answer one. keeping the envelope in
// one place means the handlers only ever deal in results and errors and
// never assemble a reply by hand.
//
// the distinction that matters most here is between a request and a
// notification. a request carries an id and must be answered exactly
// once; a notification has no id and must never be answered, not even
// when it fails. sending a reply to a notification is a protocol
// violation that will confuse a conforming client.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_PROTOCOL_H
#define MCP_PROTOCOL_H

#include <string>

#include "json/value.h"

namespace mcp {

//
// the JSON-RPC version string this server speaks.
//
inline constexpr const char *jsonrpc_version = "2.0";

//
// the MCP revision this server implements.
//
inline constexpr const char *protocol_version = "2025-06-18";

//
// standard JSON-RPC error codes.
//
namespace error_code {

// the message was not valid JSON.
inline constexpr int parse_error = -32700;

// valid JSON, but not a valid JSON-RPC request object.
inline constexpr int invalid_request = -32600;

// no handler claimed the method.
inline constexpr int method_not_found = -32601;

// the method exists but the parameters are wrong.
inline constexpr int invalid_params = -32602;

// the handler failed for a reason the caller cannot fix.
inline constexpr int internal_error = -32603;

} // namespace error_code

//
// one parsed incoming message.
//
struct request {
    // the request id, echoed back in the reply. null for a
    // notification, which must not be answered.
    json::value id;

    // the method name, for instance "tools/call".
    std::string method;

    // the params object; null when the caller sent none.
    json::value params;

    //
    // Returns:
    //      true when this message carries no id and so expects no
    //      reply.
    //
    bool is_notification() const;
};

//
// Parse a JSON-RPC message.
//
// Parameters:
//      document    - a parsed JSON value, expected to be an object.
//      out         - filled in on success.
//      error       - set to a reason on failure.
//
// Returns:
//      true when the document is a well formed request or
//      notification.
//
bool parse_request(const json::value &document, request &out,
                   std::string &error);

//
// Build a successful reply.
//
// Parameters:
//      id          - the id from the request being answered.
//      result      - the method's result value.
//
// Returns:
//      a complete JSON-RPC response object.
//
json::value make_response(const json::value &id, json::value result);

//
// Build an error reply.
//
// Parameters:
//      id          - the id from the request; null when it could not be
//                    determined, which is legal for a parse error.
//      code        - one of the error_code constants.
//      message     - a short human readable explanation.
//      data        - optional extra detail, omitted when null.
//
// Returns:
//      a complete JSON-RPC error response object.
//
json::value make_error(const json::value &id, int code,
                       const std::string &message,
                       json::value data = json::value());

} // namespace mcp

#endif // MCP_PROTOCOL_H
