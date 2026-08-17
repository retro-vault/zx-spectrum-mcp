//
// the set of tools a server offers.
//
// owns the tool objects and answers the two questions the protocol
// asks: what tools are there, and which one is called this. names are
// unique; registering a name twice replaces the earlier tool, which
// makes it possible to override one in a test without unpicking the
// registration order.
//
// listing order is registration order, so tools/list stays stable
// between runs and reads sensibly to a human.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_TOOL_REGISTRY_H
#define MCP_TOOL_REGISTRY_H

#include <memory>
#include <string_view>
#include <vector>

#include "json/value.h"
#include "mcp/tool.h"

namespace mcp {

//
// a collection of tools, keyed by name.
//
class tool_registry {
public:
    //
    // Add a tool, taking ownership.
    //
    // Notes:
    //      a null pointer is ignored. an existing tool with the same
    //      name is replaced in place, keeping its listing position.
    //
    void add(std::unique_ptr<tool> item);

    //
    // Look a tool up by name.
    //
    // Returns:
    //      the tool, or nullptr when no such name is registered.
    //
    tool *find(std::string_view name) const;

    //
    // Returns:
    //      the array of tool descriptions for a tools/list reply.
    //
    json::value list() const;

    //
    // Returns: how many tools are registered.
    //
    std::size_t size() const;

private:
    std::vector<std::unique_ptr<tool>> tools_;
};

} // namespace mcp

#endif // MCP_TOOL_REGISTRY_H
