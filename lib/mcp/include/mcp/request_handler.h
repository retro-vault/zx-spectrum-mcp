//
// chain of responsibility over the JSON-RPC methods.
//
// each protocol method gets its own handler. a message is offered to
// the first link, which either claims it or passes it along, and the
// chain ends in a handler that claims everything and reports "method
// not found". the server itself contains no dispatch logic at all.
//
// the shape is the template method pattern layered on top: handle() is
// the fixed traversal and is not virtual, while can_handle() and
// process() are what a concrete handler supplies. a subclass therefore
// cannot accidentally break the traversal.
//
// a handler returns nullopt to mean "no reply", which is the correct
// and required answer to a notification.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_REQUEST_HANDLER_H
#define MCP_REQUEST_HANDLER_H

#include <memory>
#include <optional>

#include "json/value.h"
#include "mcp/protocol.h"

namespace mcp {

//
// one link of the dispatch chain.
//
class request_handler {
public:
    virtual ~request_handler() = default;

    //
    // Returns: a short name, used in diagnostics.
    //
    virtual const char *name() const = 0;

    //
    // Attach a handler to the end of the chain.
    //
    // Parameters:
    //      next        - the handler to append; ownership is taken.
    //
    // Notes:
    //      appends at the tail rather than displacing this handler's
    //      successor, so a chain can be built by repeated calls in
    //      priority order.
    //
    void append(std::unique_ptr<request_handler> next);

    //
    // Offer a message to this handler and, failing that, to the rest of
    // the chain.
    //
    // Parameters:
    //      message     - the parsed request or notification.
    //
    // Returns:
    //      the reply to send, or nullopt when nothing should be sent.
    //
    std::optional<json::value> handle(const request &message);

protected:
    //
    // Returns: true when this handler deals with the message.
    //
    virtual bool can_handle(const request &message) const = 0;

    //
    // Do the work. only called when can_handle() returned true.
    //
    virtual std::optional<json::value> process(const request &message) = 0;

private:
    std::unique_ptr<request_handler> next_;
};

} // namespace mcp

#endif // MCP_REQUEST_HANDLER_H
