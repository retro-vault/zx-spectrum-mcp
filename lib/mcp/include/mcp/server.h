//
// the MCP server: parse, dispatch, reply, repeat.
//
// the server owns the handler chain and knows nothing about what any
// individual method does. its whole job is to turn a line of text into
// a request, hand it to the chain, and turn whatever comes back into a
// line of text.
//
// everything that can go wrong before dispatch is answered rather than
// thrown: bad JSON becomes a parse error, a malformed envelope becomes
// an invalid request, and a tool that throws becomes an internal error
// naming the tool. a server that runs unattended behind a pipe has to
// stay up, so no input can be allowed to end the loop except the peer
// closing it.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "mcp/handlers.h"
#include "mcp/request_handler.h"
#include "mcp/tool_registry.h"
#include "mcp/transport.h"

namespace mcp {

//
// a tools-only MCP server.
//
class server {
public:
    //
    // Parameters:
    //      info        - the name and version reported to the client.
    //      tools       - the tool set; must outlive the server.
    //
    // Notes:
    //      builds the standard handler chain: initialize, initialized,
    //      tools/list, tools/call, ping, then the fallback.
    //
    server(server_info info, tool_registry &tools);

    //
    // Install a diagnostic sink.
    //
    // Parameters:
    //      sink        - called with one line per event. must not write
    //                    to stdout, which belongs to the protocol.
    //
    void set_logger(std::function<void(std::string_view)> sink);

    //
    // Handle one incoming message.
    //
    // Parameters:
    //      text        - the raw message.
    //
    // Returns:
    //      the reply to send, or nullopt when the message was a
    //      notification and needs none.
    //
    // Notes:
    //      exposed so tests can drive the server one message at a time
    //      without a transport.
    //
    std::optional<std::string> handle_message(std::string_view text);

    //
    // Read, dispatch and reply until the transport closes.
    //
    void run(transport &io);

    //
    // Returns: what the handshake established.
    //
    const session_state &session() const;

private:
    void log(std::string_view line) const;

    server_info info_;
    tool_registry &tools_;
    session_state session_;
    std::unique_ptr<request_handler> chain_;
    std::function<void(std::string_view)> logger_;
};

} // namespace mcp

#endif // MCP_SERVER_H
