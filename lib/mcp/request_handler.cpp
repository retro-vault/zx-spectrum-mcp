//
// implementation of the chain traversal.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/request_handler.h"

namespace mcp {

void request_handler::append(std::unique_ptr<request_handler> next)
{
    request_handler *tail = this;
    while (tail->next_)
        tail = tail->next_.get();

    tail->next_ = std::move(next);
}

std::optional<json::value> request_handler::handle(const request &message)
{
    if (can_handle(message))
        return process(message);

    if (next_)
        return next_->handle(message);

    // the chain should always end in a handler that claims everything,
    // so reaching here means it was built wrong. answering rather than
    // going silent keeps a client from waiting forever.
    if (message.is_notification())
        return std::nullopt;

    return make_error(message.id, error_code::method_not_found,
                      "no handler is registered for method '" +
                          message.method + "'");
}

} // namespace mcp
