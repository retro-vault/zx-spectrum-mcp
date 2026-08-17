//
// the whole 48K Spectrum, assembled and clocked.
//
// this is the facade the tools talk to. it owns the CPU, the memory,
// the ULA, the key matrix and the port bus, wires them together, and
// runs the master clock that keeps them in step.
//
// the clock is the important part. one call to tick_once() advances the
// CPU by a single T-state, then advances the ULA by that T-state plus
// however many more the bus made the CPU wait for. the CPU is simply
// not ticked during a stall, which is what /WAIT does in hardware, so
// contention lengthens instructions and moves the beam at the same time
// without either side knowing about the other.
//
// machine implements bus_interface, so every memory and port access the
// CPU makes arrives here, which is also where breakpoints are tested.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_MACHINE_H
#define SPECTRUM_MACHINE_H

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "spectrum/breakpoints.h"
#include "spectrum/contention.h"
#include "spectrum/cpu.h"
#include "spectrum/io_bus.h"
#include "spectrum/interface1_serial.h"
#include "spectrum/keyboard.h"
#include "spectrum/memory.h"
#include "spectrum/tape.h"
#include "spectrum/timing.h"
#include "spectrum/types.h"
#include "spectrum/ula.h"

namespace spectrum {

//
// why a run stopped.
//
enum class stop_reason : u8 {
    // the requested amount of work was completed.
    completed,

    // a breakpoint fired.
    breakpoint,

    // the CPU entered HALT and the caller asked to stop there.
    halted,

    // execution reached the requested address.
    address_reached,

    // the instruction budget ran out.
    step_limit,
};

//
// Returns: the wire name of a stop reason.
//
const char *stop_reason_name(stop_reason reason);

//
// when to stop running. every limit is optional; the first one reached
// ends the run. a run with no limit at all is capped at one frame so a
// runaway program cannot hang the server.
//
struct run_limits {
    // 0 means no limit.
    u64 max_tstates = 0;
    u64 max_instructions = 0;
    u64 max_frames = 0;

    // stop when the program counter reaches this address, tested at
    // instruction boundaries only.
    std::optional<u16> until_pc;

    // stop as soon as the CPU is halted.
    bool stop_on_halt = false;
};

//
// what a run actually did.
//
struct run_result {
    stop_reason reason = stop_reason::completed;

    u64 tstates = 0;
    u64 instructions = 0;
    u64 frames = 0;

    // id of the breakpoint that fired, or -1.
    int breakpoint_id = -1;

    // program counter when the run stopped.
    u16 pc = 0;
};

//
// a complete emulated machine.
//
class machine final : public bus_interface {
public:
    using frame_observer =
        std::function<void(const framebuffer &frame, u64 frame_number)>;

    explicit machine(
        const machine_timing &timing = zx_spectrum_48k_timing);
    ~machine() override;

    machine(const machine &) = delete;
    machine &operator=(const machine &) = delete;

    //
    // Restart the machine.
    //
    // Parameters:
    //      clear_memory    - also wipe RAM. a real reset does not, and
    //                        the ROM relies on that to offer NEW; pass
    //                        true for a guaranteed clean slate.
    //
    void reset(bool clear_memory = false);

    //
    // Run until one of the limits is reached.
    //
    // Returns:
    //      what was executed and why it stopped.
    //
    run_result run(const run_limits &limits);

    // Run for a number of T-states.
    run_result run_tstates(u64 count);

    // Run for a number of complete frames.
    run_result run_frames(u64 count);

    // Execute a number of instructions.
    run_result step(u64 instructions);

    //
    // Run until the program counter reaches an address.
    //
    // Parameters:
    //      address         - address to stop at.
    //      max_tstates     - give up after this long, so a target that
    //                        is never reached still returns.
    //
    run_result run_until(u16 address, u64 max_tstates);

    memory &mem();
    const memory &mem() const;

    ula &video();
    const ula &video() const;

    keyboard &keys();
    const keyboard &keys() const;

    cpu &processor();
    const cpu &processor() const;

    //
    // Registers as a debugger should see them.
    //
    // Notes:
    //      identical to processor().registers() except for pc. the CPU
    //      core overlaps the opcode fetch of the next instruction with
    //      the tail of the current one, so its raw pc has already moved
    //      past the opcode that is about to execute. this reports the
    //      address of that instruction instead, which is what a
    //      disassembler, a breakpoint and a snapshot all want.
    //
    cpu_registers registers() const;

    //
    // Returns:
    //      address of the instruction about to execute, valid when the
    //      CPU is on an instruction boundary.
    //
    u16 instruction_address() const;

    io_bus &ports();

    //
    // Attach the serial portion of ZX Interface 1 to the I/O bus.
    //
    // Notes:
    //      This exposes the hardware ports without opening host sockets and
    //      is primarily useful to embedded callers and hardware tests.
    //
    void enable_interface1_serial();

    //
    // Install the 8K Interface 1 shadow ROM and attach its serial ports.
    //
    // Returns:
    //      true when the image has the required size.
    //
    bool load_interface1_rom(std::span<const u8> data,
                             std::string &error);

    //
    // Attach Interface 1 serial and open its data/control TCP listeners.
    //
    // Returns:
    //      true on success; otherwise error describes the socket failure.
    //
    bool enable_serial_bridge(const serial_bridge_config &config,
                              std::string &error);

    // Return whether the Interface 1 serial ports are on the I/O bus.
    bool interface1_serial_enabled() const;

    // Access the Interface 1 serial device.
    interface1_serial &serial();
    const interface1_serial &serial() const;

    // Access the cassette transport connected to the ULA EAR input.
    tape_deck &tape();
    const tape_deck &tape() const;

    breakpoint_set &breakpoints();
    const breakpoint_set &breakpoints() const;

    const machine_timing &timing() const;
    const contention_model &contention() const;

    //
    // Replace the contention strategy, for instance with no_contention
    // to run without ULA interference.
    //
    void set_contention(std::unique_ptr<contention_model> model);

    // T-states since the last reset.
    u64 total_tstates() const;

    // T-states since the start of the current frame.
    u32 frame_tstate() const;

    // completed frames since the last reset.
    u64 frame_number() const;

    //
    // Observe each fully rendered frame. Passing an empty function removes
    // the observer. There is deliberately only one observer: it is a host
    // integration hook, currently owned by the video recorder, rather than
    // part of the emulated hardware.
    //
    void set_frame_observer(frame_observer observer);

    // instructions retired since the last reset.
    u64 instruction_count() const;

    //
    // Read a port directly, without the CPU.
    //
    // Notes:
    //      for the diagnostic tools. no contention is applied and no
    //      time passes, so this cannot disturb timing.
    //
    u8 read_port(u16 port);

    //
    // Write a port directly, without the CPU.
    //
    void write_port(u16 port, u8 value);

    u8 opcode_fetch(u16 address) override;
    u8 memory_read(u16 address) override;
    void memory_write(u16 address, u8 value) override;
    u8 io_read(u16 port) override;
    void io_write(u16 port, u8 value) override;
    u8 interrupt_acknowledge() override;
    int memory_wait_states(u16 address, int tstates_since_start) override;
    int io_wait_states(u16 port, int tstates_since_start) override;

private:
    //
    // Advance the CPU by one T-state and the clock by one T-state plus
    // whatever stall the bus imposed.
    //
    void tick_once();

    //
    // Advance the master clock, ticking the ULA for each T-state and
    // rolling over at the end of a frame.
    //
    void advance_clock(int tstates);

    //
    // Convert a machine cycle start correction into a frame position.
    //
    u32 cycle_tstate(int tstates_since_start) const;

    //
    // Record a data or i/o breakpoint hit, to be acted on once the
    // current instruction finishes.
    //
    void note_breakpoint(breakpoint_kind kind, u16 address, u8 value);

    // Read through the optional Interface 1 shadow-ROM overlay.
    u8 mapped_memory_read(u16 address) const;

    machine_timing timing_;
    u32 frame_length_;

    memory memory_;
    keyboard keyboard_;
    std::unique_ptr<contention_model> contention_;
    ula ula_;
    tape_deck tape_;
    interface1_serial serial_;
    io_bus ports_;
    cpu cpu_;
    breakpoint_set breakpoints_;

    u64 total_tstates_ = 0;
    u32 frame_tstate_ = 0;
    u64 frame_number_ = 0;
    u64 instructions_ = 0;
    frame_observer frame_observer_;

    // address of the most recent opcode fetch. on an instruction
    // boundary this is the instruction that is about to execute.
    u16 instruction_address_ = 0;

    // breakpoint seen mid instruction, acted on at the next boundary.
    int pending_breakpoint_ = -1;
    bool serial_attached_ = false;
};

} // namespace spectrum

#endif // SPECTRUM_MACHINE_H
