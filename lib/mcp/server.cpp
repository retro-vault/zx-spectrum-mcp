//
// implementation of the server loop.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/server.h"

#include <exception>

#include "json/parser.h"
#include "json/writer.h"

namespace mcp {

server::server(server_info info, tool_registry &tools)
    : info_(std::move(info)), tools_(tools)
{
    // built in priority order. the fallback claims anything left, so
    // the chain can never run off its end.
    chain_ = std::make_unique<initialize_handler>(info_, session_);
    chain_->append(std::make_unique<initialized_handler>(session_));
    chain_->append(std::make_unique<tools_list_handler>(tools_));
    chain_->append(std::make_unique<tools_call_handler>(tools_));
    chain_->append(std::make_unique<ping_handler>());
    chain_->append(std::make_unique<fallback_handler>());
}

void server::set_logger(std::function<void(std::string_view)> sink)
{
    logger_ = std::move(sink);
}

void server::log(std::string_view line) const
{
    if (logger_)
        logger_(line);
}

std::optional<std::string> server::handle_message(std::string_view text)
{
    const json::parse_result parsed = json::parse(text);

    if (!parsed) {
        log("parse error: " + parsed.error);

        // the id cannot be known when the message did not parse, and
        // the spec says to answer with a null id in that case.
        return json::write(make_error(json::value(),
                                      error_code::parse_error,
                                      "invalid JSON: " + parsed.error));
    }

    // JSON-RPC batching was dropped from MCP, and supporting it halfway
    // would be worse than saying so.
    if (parsed.document.is_array()) {
        return json::write(make_error(
            json::value(), error_code::invalid_request,
            "batched requests are not supported; send one message per "
            "line"));
    }

    request message;
    std::string error;

    if (!parse_request(parsed.document, message, error)) {
        log("invalid request: " + error);
        return json::write(make_error(parsed.document["id"],
                                      error_code::invalid_request,
                                      error));
    }

    log("-> " + message.method);

    std::optional<json::value> reply;

    // a tool is arbitrary code. if one throws, the session must
    // survive it and the caller must be told which tool failed.
    try {
        reply = chain_->handle(message);
    } catch (const std::exception &e) {
        log(std::string("handler threw: ") + e.what());
        if (message.is_notification())
            return std::nullopt;
        return json::write(make_error(
            message.id, error_code::internal_error,
            std::string("method '") + message.method +
                "' failed: " + e.what()));
    } catch (...) {
        log("handler threw an unknown exception");
        if (message.is_notification())
            return std::nullopt;
        return json::write(make_error(message.id,
                                      error_code::internal_error,
                                      "method '" + message.method +
                                          "' failed"));
    }

    if (!reply)
        return std::nullopt;

    return json::write(*reply);
}

void server::run(transport &io)
{
    std::string incoming;

    while (io.read_message(incoming)) {
        if (std::optional<std::string> reply = handle_message(incoming))
            io.write_message(*reply);
    }

    log("transport closed");
}

const session_state &server::session() const { return session_; }

} // namespace mcp
