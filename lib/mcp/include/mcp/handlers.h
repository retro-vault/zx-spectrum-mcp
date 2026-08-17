//
// the concrete handlers for the MCP methods this server implements.
//
// the set is deliberately small. a tools-only server has to answer
// initialize, accept the initialized notification, list its tools, call
// them, and reply to ping. everything else belongs to capabilities this
// server does not advertise, and advertising nothing it cannot do is
// what keeps it honest with a conforming client.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_HANDLERS_H
#define MCP_HANDLERS_H

#include <string>

#include "mcp/request_handler.h"
#include "mcp/tool_registry.h"

namespace mcp {

//
// who this server says it is during initialize.
//
struct server_info {
    std::string name;
    std::string version;
};

//
// what the handshake established.
//
struct session_state {
    // set once the client's initialize has been answered.
    bool initialized = false;

    // set when the client's initialized notification arrives.
    bool ready = false;

    // what the client called itself.
    std::string client_name;
    std::string client_version;

    // the protocol revision the two sides settled on.
    std::string protocol_version;
};

//
// answers "initialize" and records the negotiated protocol version.
//
class initialize_handler final : public request_handler {
public:
    initialize_handler(server_info info, session_state &session);

    const char *name() const override;

protected:
    bool can_handle(const request &message) const override;
    std::optional<json::value> process(const request &message) override;

private:
    server_info info_;
    session_state &session_;
};

//
// swallows "notifications/initialized", which completes the handshake
// and must not be answered.
//
class initialized_handler final : public request_handler {
public:
    explicit initialized_handler(session_state &session);

    const char *name() const override;

protected:
    bool can_handle(const request &message) const override;
    std::optional<json::value> process(const request &message) override;

private:
    session_state &session_;
};

//
// answers "ping" with an empty result, the protocol's keepalive.
//
class ping_handler final : public request_handler {
public:
    const char *name() const override;

protected:
    bool can_handle(const request &message) const override;
    std::optional<json::value> process(const request &message) override;
};

//
// answers "tools/list" from the registry.
//
class tools_list_handler final : public request_handler {
public:
    explicit tools_list_handler(const tool_registry &tools);

    const char *name() const override;

protected:
    bool can_handle(const request &message) const override;
    std::optional<json::value> process(const request &message) override;

private:
    const tool_registry &tools_;
};

//
// answers "tools/call" by looking the tool up and invoking it.
//
// a tool that fails at its job reports that inside the result, with
// isError set, because the model needs to see it. only a malformed call
// or an unknown name becomes a JSON-RPC error.
//
class tools_call_handler final : public request_handler {
public:
    explicit tools_call_handler(tool_registry &tools);

    const char *name() const override;

protected:
    bool can_handle(const request &message) const override;
    std::optional<json::value> process(const request &message) override;

private:
    tool_registry &tools_;
};

//
// claims anything that reached it, so the chain always terminates.
//
class fallback_handler final : public request_handler {
public:
    const char *name() const override;

protected:
    bool can_handle(const request &message) const override;
    std::optional<json::value> process(const request &message) override;
};

} // namespace mcp

#endif // MCP_HANDLERS_H
