//
// implementation of the tool registry.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/tool_registry.h"

namespace mcp {

void tool_registry::add(std::unique_ptr<tool> item)
{
    if (!item)
        return;

    const std::string name = item->name();

    for (std::unique_ptr<tool> &existing : tools_) {
        if (existing->name() == name) {
            existing = std::move(item);
            return;
        }
    }

    tools_.push_back(std::move(item));
}

tool *tool_registry::find(std::string_view name) const
{
    for (const std::unique_ptr<tool> &item : tools_) {
        if (item->name() == name)
            return item.get();
    }
    return nullptr;
}

json::value tool_registry::list() const
{
    json::value entries = json::value::make_array();
    for (const std::unique_ptr<tool> &item : tools_)
        entries.push_back(item->describe());
    return entries;
}

std::size_t tool_registry::size() const { return tools_.size(); }

} // namespace mcp
