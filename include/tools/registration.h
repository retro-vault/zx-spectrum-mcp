//
// registration of the emulator's tools with an MCP registry.
//
// the tools are grouped by what they touch, one translation unit per
// group, and each group exposes a single registration function. main()
// calls register_all_tools() and never sees an individual tool class,
// which is what lets a group be added or left out without editing
// anything else.
//
// listing order is registration order, so this is also the order a
// client sees in tools/list. the groups are ordered the way somebody
// would use them: get a program in, run it, look at the state, then
// look at the screen.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef TOOLS_REGISTRATION_H
#define TOOLS_REGISTRATION_H

#include "mcp/tool_registry.h"
#include "spectrum/machine.h"

namespace tools {

//
// Register the load tool.
//
// Parameters:
//      registry    - where the tool is added.
//      target      - the machine it drives; must outlive the registry.
//
// Notes:
//      kept apart from the other machine tools because format dispatch
//      makes it as large as all of them together.
//
void register_load_tool(mcp::tool_registry &registry,
                        spectrum::machine &target);

// Register cassette transport control and diagnostics.
void register_tape_tools(mcp::tool_registry &registry,
                         spectrum::machine &target);

//
// Register running, stepping, resetting and status.
//
// Parameters:
//      registry    - where the tools are added.
//      target      - the machine they drive; must outlive the
//                    registry.
//
void register_machine_tools(mcp::tool_registry &registry,
                            spectrum::machine &target);

//
// Register memory reading and writing.
//
void register_memory_tools(mcp::tool_registry &registry,
                           spectrum::machine &target);

//
// Register register inspection and breakpoints.
//
void register_cpu_tools(mcp::tool_registry &registry,
                        spectrum::machine &target);

//
// Register the keyboard and the i/o ports.
//
void register_io_tools(mcp::tool_registry &registry,
                       spectrum::machine &target);

//
// Register the screen as an image and as text.
//
void register_screen_tools(mcp::tool_registry &registry,
                           spectrum::machine &target);

//
// Register persistent PNG screenshot and video recording tools.
//
void register_capture_tools(mcp::tool_registry &registry,
                            spectrum::machine &target);

//
// Register every group, in the order above.
//
void register_all_tools(mcp::tool_registry &registry,
                        spectrum::machine &target);

} // namespace tools

#endif // TOOLS_REGISTRATION_H
