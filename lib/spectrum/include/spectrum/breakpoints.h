//
// breakpoints on execution, on data access and on i/o.
//
// the set is consulted on every bus cycle, so the common case of having
// no breakpoints at all has to cost as close to nothing as possible. a
// per-kind counter short circuits that case, and a per-kind bitmap over
// the whole address space rejects an uninteresting address with one bit
// test before any list is walked.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_BREAKPOINTS_H
#define SPECTRUM_BREAKPOINTS_H

#include <array>
#include <bitset>
#include <optional>
#include <string_view>
#include <vector>

#include "spectrum/types.h"

namespace spectrum {

//
// what kind of access trips a breakpoint.
//
enum class breakpoint_kind : u8 {
    // the CPU is about to execute the instruction at this address.
    execute,

    // a data byte was read from this address.
    memory_read,

    // a data byte was written to this address.
    memory_write,

    // this port was read.
    io_read,

    // this port was written.
    io_write,
};

//
// how many kinds there are; sizes the per-kind lookup tables.
//
inline constexpr std::size_t breakpoint_kind_count = 5;

//
// Returns: the wire name of a kind, such as "memory_write".
//
const char *breakpoint_kind_name(breakpoint_kind kind);

//
// Parse a kind from its wire name.
//
// Returns: the kind, or nullopt when the name is unknown.
//
std::optional<breakpoint_kind> breakpoint_kind_from_name(
    std::string_view name);

//
// one armed breakpoint.
//
struct breakpoint {
    int id = 0;
    breakpoint_kind kind = breakpoint_kind::execute;
    u16 address = 0;

    // when set, the breakpoint only fires if the byte transferred
    // equals this. ignored for execute breakpoints.
    std::optional<u8> value;

    bool enabled = true;
    u64 hits = 0;
};

//
// the collection of breakpoints for one machine.
//
class breakpoint_set {
public:
    //
    // Arm a new breakpoint.
    //
    // Parameters:
    //      kind        - what kind of access to watch.
    //      address     - address or port to watch.
    //      value       - optional byte value that must match.
    //
    // Returns:
    //      the new breakpoint's id, unique for the life of the set.
    //
    int add(breakpoint_kind kind, u16 address,
            std::optional<u8> value = std::nullopt);

    //
    // Remove a breakpoint.
    //
    // Returns: true when the id existed.
    //
    bool remove(int id);

    //
    // Enable or disable a breakpoint without removing it.
    //
    // Returns: true when the id existed.
    //
    bool set_enabled(int id, bool enabled);

    //
    // Remove every breakpoint. ids are not reused afterwards.
    //
    void clear();

    // Returns: every breakpoint, in creation order.
    const std::vector<breakpoint> &all() const;

    // Returns: the breakpoint with this id, or nullptr.
    const breakpoint *find(int id) const;

    //
    // Test an access against the armed breakpoints.
    //
    // Parameters:
    //      kind        - the access that just happened.
    //      address     - address or port involved.
    //      value       - byte transferred; ignored for execute.
    //
    // Returns:
    //      the breakpoint that fired, with its hit count already
    //      advanced, or nullptr when none matched.
    //
    // Notes:
    //      designed to be called on every bus cycle. returns without
    //      touching memory when nothing of that kind is armed.
    //
    breakpoint *match(breakpoint_kind kind, u16 address, u8 value);

    //
    // Returns:
    //      true when at least one enabled breakpoint of this kind
    //      exists. lets the caller skip the call entirely.
    //
    bool armed(breakpoint_kind kind) const;

private:
    std::vector<breakpoint> items_;
    int next_id_ = 1;

    // one address bitmap and one counter per kind.
    std::array<std::bitset<0x10000>, breakpoint_kind_count> index_{};
    std::array<int, breakpoint_kind_count> armed_count_{};

    void rebuild_index();
};

} // namespace spectrum

#endif // SPECTRUM_BREAKPOINTS_H
