//
// implementation of the breakpoint set.
//
// the index is rebuilt wholesale whenever the collection changes.
// breakpoints are added by hand, a few at a time, so an O(n) rebuild is
// free compared with the cost of getting incremental removal wrong.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/breakpoints.h"

#include <algorithm>

namespace spectrum {

namespace {

struct kind_name {
    breakpoint_kind kind;
    const char *name;
};

constexpr kind_name kind_names[] = {
    {breakpoint_kind::execute, "execute"},
    {breakpoint_kind::memory_read, "memory_read"},
    {breakpoint_kind::memory_write, "memory_write"},
    {breakpoint_kind::io_read, "io_read"},
    {breakpoint_kind::io_write, "io_write"},
};

constexpr std::size_t index_of(breakpoint_kind kind)
{
    return static_cast<std::size_t>(kind);
}

} // namespace

const char *breakpoint_kind_name(breakpoint_kind kind)
{
    for (const kind_name &entry : kind_names) {
        if (entry.kind == kind)
            return entry.name;
    }
    return "execute";
}

std::optional<breakpoint_kind> breakpoint_kind_from_name(
    std::string_view name)
{
    for (const kind_name &entry : kind_names) {
        if (name == entry.name)
            return entry.kind;
    }
    return std::nullopt;
}

int breakpoint_set::add(breakpoint_kind kind, u16 address,
                        std::optional<u8> value)
{
    breakpoint item;
    item.id = next_id_++;
    item.kind = kind;
    item.address = address;
    item.value = value;
    item.enabled = true;
    item.hits = 0;

    items_.push_back(item);
    rebuild_index();
    return item.id;
}

bool breakpoint_set::remove(int id)
{
    const auto it = std::find_if(
        items_.begin(), items_.end(),
        [id](const breakpoint &b) { return b.id == id; });

    if (it == items_.end())
        return false;

    items_.erase(it);
    rebuild_index();
    return true;
}

bool breakpoint_set::set_enabled(int id, bool enabled)
{
    const auto it = std::find_if(
        items_.begin(), items_.end(),
        [id](const breakpoint &b) { return b.id == id; });

    if (it == items_.end())
        return false;

    it->enabled = enabled;
    rebuild_index();
    return true;
}

void breakpoint_set::clear()
{
    items_.clear();
    rebuild_index();
}

const std::vector<breakpoint> &breakpoint_set::all() const
{
    return items_;
}

const breakpoint *breakpoint_set::find(int id) const
{
    const auto it = std::find_if(
        items_.begin(), items_.end(),
        [id](const breakpoint &b) { return b.id == id; });

    return it != items_.end() ? &*it : nullptr;
}

void breakpoint_set::rebuild_index()
{
    for (auto &bits : index_)
        bits.reset();
    armed_count_.fill(0);

    for (const breakpoint &item : items_) {
        if (!item.enabled)
            continue;
        const std::size_t slot = index_of(item.kind);
        index_[slot].set(item.address);
        ++armed_count_[slot];
    }
}

bool breakpoint_set::armed(breakpoint_kind kind) const
{
    return armed_count_[index_of(kind)] > 0;
}

breakpoint *breakpoint_set::match(breakpoint_kind kind, u16 address,
                                  u8 value)
{
    const std::size_t slot = index_of(kind);

    // two cheap rejections before any list walk: nothing of this kind
    // armed at all, then nothing armed at this address.
    if (armed_count_[slot] == 0)
        return nullptr;
    if (!index_[slot].test(address))
        return nullptr;

    for (breakpoint &item : items_) {
        if (!item.enabled || item.kind != kind || item.address != address)
            continue;

        // a value filter never applies to execution: there is no
        // transferred byte to compare.
        if (item.value && kind != breakpoint_kind::execute &&
            *item.value != value)
            continue;

        ++item.hits;
        return &item;
    }

    return nullptr;
}

} // namespace spectrum
