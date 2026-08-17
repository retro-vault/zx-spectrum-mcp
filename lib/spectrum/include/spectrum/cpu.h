//
// Z80 CPU, wrapped so the rest of the system never sees the pin level
// protocol of the vendored core.
//
// the core is driven one T-state at a time and communicates through a
// 64 bit pin mask. that mask, and the header that defines it, stay
// inside cpu.cpp: the vendored header relies on anonymous unions that
// are a GCC extension in C++, and letting it leak would force every
// translation unit that touches the CPU to relax -pedantic.
//
// what the rest of the system sees instead is bus_interface. the CPU
// calls into it whenever it starts a machine cycle, and the machine
// answers with data and with how many T-states the bus made it wait.
//
// contention is *not* applied through the core's /WAIT pin. the core
// samples /WAIT before it puts an address on the bus for every machine
// cycle except the opcode fetch, so a decision that depends on the
// address cannot be made in time. instead tick() reports the stall back
// to the caller, which freezes the CPU and lets the ULA run on for that
// many T-states. that is what /WAIT does physically, and it keeps the
// timing decision where the information actually is.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_CPU_H
#define SPECTRUM_CPU_H

#include <memory>

#include "spectrum/types.h"

namespace spectrum {

//
// the complete programmer visible CPU state.
//
struct cpu_registers {
    u16 af = 0;
    u16 bc = 0;
    u16 de = 0;
    u16 hl = 0;

    // the shadow bank reached with EX AF,AF' and EXX.
    u16 af_alt = 0;
    u16 bc_alt = 0;
    u16 de_alt = 0;
    u16 hl_alt = 0;

    u16 ix = 0;
    u16 iy = 0;
    u16 sp = 0;
    u16 pc = 0;

    // the undocumented internal pointer, MEMPTR. visible only through
    // the flags left by BIT n,(HL), but software does depend on it.
    u16 wz = 0;

    u8 i = 0;
    u8 r = 0;
    u8 im = 0;

    bool iff1 = false;
    bool iff2 = false;
    bool halted = false;
};

//
// everything the CPU can reach outside itself.
//
// implemented by the machine. each method is called at the moment the
// CPU starts the corresponding machine cycle.
//
class bus_interface {
public:
    virtual ~bus_interface() = default;

    //
    // Read the byte at address as an instruction opcode, an M1 cycle.
    //
    // Notes:
    //      kept apart from memory_read so execution breakpoints can be
    //      told from data access breakpoints.
    //
    virtual u8 opcode_fetch(u16 address) = 0;

    // Read a data byte.
    virtual u8 memory_read(u16 address) = 0;

    // Write a data byte.
    virtual void memory_write(u16 address, u8 value) = 0;

    // Read an i/o port.
    virtual u8 io_read(u16 port) = 0;

    // Write an i/o port.
    virtual void io_write(u16 port, u8 value) = 0;

    //
    // Supply the byte the CPU reads during an interrupt acknowledge.
    //
    // Returns:
    //      whatever the hardware puts on the bus. on an unexpanded 48K
    //      nothing drives it, so it reads 0xFF, which is why interrupt
    //      mode 2 tables are conventionally filled with 0xFF bytes.
    //
    virtual u8 interrupt_acknowledge() = 0;

    //
    // Report how long the bus makes a memory cycle wait.
    //
    // Parameters:
    //      address              - address on the bus.
    //      tstates_since_start  - how many T-states ago the machine
    //                             cycle really began. the vendored core
    //                             presents its address bus a fixed
    //                             number of T-states late depending on
    //                             the cycle type, and this is that
    //                             correction, so the caller can sample
    //                             the contention table at the T-state
    //                             the real ULA would have.
    //
    // Returns:
    //      extra T-states to stall, 0 when the access is free.
    //
    virtual int memory_wait_states(u16 address,
                                   int tstates_since_start) = 0;

    //
    // Report how long the bus makes an i/o cycle wait.
    //
    // Parameters:
    //      port                 - full 16 bit port address.
    //      tstates_since_start  - as for memory_wait_states.
    //
    // Returns:
    //      extra T-states on top of the four the cycle already takes.
    //
    virtual int io_wait_states(u16 port, int tstates_since_start) = 0;
};

//
// the Z80 itself.
//
class cpu {
public:
    cpu();
    ~cpu();

    cpu(const cpu &) = delete;
    cpu &operator=(const cpu &) = delete;

    //
    // Put the CPU into its reset state: PC and IR cleared, interrupts
    // disabled, interrupt mode 0.
    //
    void reset();

    //
    // Run for exactly one T-state.
    //
    // Parameters:
    //      bus                 - the system the CPU talks to.
    //      interrupt_requested - level of the /INT line this T-state.
    //
    // Returns:
    //      T-states the bus asked to stall for. the caller must let
    //      that much time pass without calling tick() again, so that
    //      video and any other clocked hardware advance while the CPU
    //      is held off the bus.
    //
    int tick(bus_interface &bus, bool interrupt_requested);

    //
    // Returns:
    //      true when the last tick finished an instruction, so the next
    //      tick starts a new one. this is the boundary that single
    //      stepping and execution breakpoints use.
    //
    bool instruction_complete() const;

    //
    // Returns: true while the CPU sits in HALT.
    //
    bool halted() const;

    // Returns: a snapshot of every register.
    cpu_registers registers() const;

    //
    // Overwrite every register.
    //
    // Notes:
    //      execution restarts cleanly at regs.pc, so this is safe only
    //      at an instruction boundary. loading a snapshot is the
    //      intended use.
    //
    void set_registers(const cpu_registers &regs);

    //
    // Continue execution at an address, discarding any partly decoded
    // instruction.
    //
    void jump(u16 address);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace spectrum

#endif // SPECTRUM_CPU_H
