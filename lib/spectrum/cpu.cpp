//
// adapter between the vendored tick-stepped Z80 core and bus_interface.
//
// this is the only translation unit that includes z80.h, and the only
// one that knows what a pin mask is.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/cpu.h"

// the vendored core stores its register file in anonymous unions
// containing anonymous structs. that is valid C and a GCC extension in
// C++, and upstream says so in a comment next to the declaration. the
// warning is suppressed here, for this include only, so that every
// other file in the project still compiles clean under -pedantic.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#define CHIPS_IMPL
#include "z80/z80.h"
#pragma GCC diagnostic pop

namespace spectrum {

namespace {

//
// how late the core presents its address bus.
//
// the real chip puts an address out in T1 of every machine cycle. the
// vendored core does that for an opcode fetch, but for the other cycle
// types it asserts one or two T-states later. contention depends on
// exactly when a cycle starts, so the difference is measured once and
// corrected here rather than being absorbed silently.
//
// the offsets were measured by logging the T-state of every address bus
// assertion for instructions with known machine cycle boundaries; the
// method and the readings are written up in docs/notes/ARCHITECTURE.md.
//
// tests/test_cpu_timing.cpp guards them. it runs LD A,(nn) against
// contended memory at chosen points in the frame and requires the
// instruction to lengthen by exactly the ULA table entry for the
// T-state its final machine cycle begins on. a wrong offset here shifts
// which entry is sampled and those checks fail.
//
constexpr int opcode_fetch_lag = 0;
constexpr int memory_lag = 1;
constexpr int io_write_lag = 1;
constexpr int io_read_lag = 2;

} // namespace

struct cpu::impl {
    enum class prefix_kind : u8 {
        none,
        indexed,
        cb,
        ed,
        indexed_cb,
    };

    z80_t core{};
    std::uint64_t pins = 0;

    // true when the previous T-state already had a bus cycle running,
    // used to spot the start of a new one.
    bool bus_active = false;

    // State which the upstream core deliberately does not expose: the
    // Q latch says whether the previous instruction changed F. Zilog
    // NMOS chips use it for the undocumented flag bits of SCF and CCF.
    bool last_instruction_changed_flags = false;
    bool instruction_active = false;
    u8 flags_before_instruction = 0;
    prefix_kind prefix = prefix_kind::none;

    void begin_instruction(u8 opcode)
    {
        instruction_active = true;
        flags_before_instruction = core.f;
        prefix = prefix_kind::none;
        observe_opcode(opcode);
    }

    void observe_opcode(u8 opcode)
    {
        // Once CB or ED has been seen, the following byte is the actual
        // opcode even when its value happens to equal another prefix.
        if (prefix == prefix_kind::cb || prefix == prefix_kind::ed ||
            prefix == prefix_kind::indexed_cb) {
            return;
        }

        if (opcode == 0xdd || opcode == 0xfd) {
            prefix = prefix_kind::indexed;
        } else if (opcode == 0xed) {
            prefix = prefix_kind::ed;
        } else if (opcode == 0xcb) {
            prefix = prefix == prefix_kind::indexed
                         ? prefix_kind::indexed_cb
                         : prefix_kind::cb;
        }
    }

    static bool base_opcode_changes_flags(u8 opcode)
    {
        if (opcode >= 0x80 && opcode <= 0xbf)
            return true; // arithmetic and logical register groups

        if ((opcode & 0xc7) == 0x04 ||
            (opcode & 0xc7) == 0x05) {
            return true; // INC/DEC r, including (HL)/(IX+d)/(IY+d)
        }

        if ((opcode & 0xcf) == 0x09)
            return true; // ADD HL/IX/IY,rr

        switch (opcode) {
        case 0x07: // RLCA
        case 0x0f: // RRCA
        case 0x17: // RLA
        case 0x1f: // RRA
        case 0x27: // DAA
        case 0x2f: // CPL
        case 0x37: // SCF
        case 0x3f: // CCF
        case 0xc6: // ADD A,n
        case 0xce: // ADC A,n
        case 0xd6: // SUB n
        case 0xde: // SBC A,n
        case 0xe6: // AND n
        case 0xee: // XOR n
        case 0xf6: // OR n
        case 0xfe: // CP n
            return true;
        default:
            return false;
        }
    }

    static bool ed_opcode_changes_flags(u8 opcode)
    {
        // IN r,(C), including the undocumented IN (C).
        if ((opcode & 0xc7) == 0x40)
            return true;

        // SBC HL,rr and ADC HL,rr.
        if ((opcode & 0xcf) == 0x42 ||
            (opcode & 0xcf) == 0x4a) {
            return true;
        }

        // All eight encodings of NEG.
        if ((opcode & 0xc7) == 0x44)
            return true;

        switch (opcode) {
        case 0x57: // LD A,I
        case 0x5f: // LD A,R
        case 0x67: // RRD
        case 0x6f: // RLD
        case 0xa0: // LDI
        case 0xa1: // CPI
        case 0xa2: // INI
        case 0xa3: // OUTI
        case 0xa8: // LDD
        case 0xa9: // CPD
        case 0xaa: // IND
        case 0xab: // OUTD
        case 0xb0: // LDIR
        case 0xb1: // CPIR
        case 0xb2: // INIR
        case 0xb3: // OTIR
        case 0xb8: // LDDR
        case 0xb9: // CPDR
        case 0xba: // INDR
        case 0xbb: // OTDR
            return true;
        default:
            return false;
        }
    }

    bool current_instruction_changes_flags() const
    {
        if (prefix == prefix_kind::cb ||
            prefix == prefix_kind::indexed_cb) {
            // Rotate/shift and BIT change F; RES and SET do not.
            return (core.opcode >> 6) < 2;
        }
        if (prefix == prefix_kind::ed)
            return ed_opcode_changes_flags(core.opcode);
        return base_opcode_changes_flags(core.opcode);
    }

    void finish_instruction()
    {
        if (!instruction_active)
            return;

        const bool base = prefix == prefix_kind::none ||
                          prefix == prefix_kind::indexed;

        // On an NMOS Z80, bits 3 and 5 from the old F register survive
        // SCF/CCF when the preceding instruction did not change flags.
        // The upstream core always takes those bits from A, so restore
        // the Q-latch-dependent result at the instruction boundary.
        if (base && (core.opcode == 0x37 || core.opcode == 0x3f)) {
            constexpr u8 undocumented = 0x28;
            const u8 sources = last_instruction_changed_flags
                                   ? core.a
                                   : static_cast<u8>(
                                         flags_before_instruction | core.a);
            core.f = static_cast<u8>((core.f & ~undocumented) |
                                     (sources & undocumented));
        }

        // The repeat path in the upstream block-I/O implementation uses
        // WZ as a temporary PC and leaves that value visible. Real chips
        // retain the port address adjustment instead.
        if (prefix == prefix_kind::ed) {
            switch (core.opcode) {
            case 0xb2: // INIR: port before B--, then +1
                core.wz = static_cast<u16>(core.bc + 0x0101);
                break;
            case 0xba: // INDR: port before B--, then -1
                core.wz = static_cast<u16>(core.bc + 0x00ff);
                break;
            case 0xb3: // OTIR: port after B--, then +1
                core.wz = static_cast<u16>(core.bc + 1);
                break;
            case 0xbb: // OTDR: port after B--, then -1
                core.wz = static_cast<u16>(core.bc - 1);
                break;
            default:
                break;
            }
        }

        last_instruction_changed_flags =
            current_instruction_changes_flags();
        instruction_active = false;
    }

    void reset_instruction_state()
    {
        last_instruction_changed_flags = false;
        instruction_active = false;
        flags_before_instruction = 0;
        prefix = prefix_kind::none;
    }

    void accept_interrupt(u8 opcode)
    {
        // Finish the instruction whose boundary admitted the interrupt,
        // then clear Q as the real interrupt sequence does. In mode 0 the
        // acknowledge byte itself starts an instruction; modes 1 and 2
        // execute an internal sequence before fetching from memory.
        finish_instruction();
        last_instruction_changed_flags = false;
        instruction_active = false;
        prefix = prefix_kind::none;
        if (core.im == 0)
            begin_instruction(opcode);
    }
};

cpu::cpu() : impl_(std::make_unique<impl>())
{
    impl_->pins = z80_init(&impl_->core);
    impl_->bus_active = false;
    impl_->reset_instruction_state();
}

cpu::~cpu() = default;

void cpu::reset()
{
    impl_->pins = z80_reset(&impl_->core);
    impl_->bus_active = false;
    impl_->reset_instruction_state();
}

int cpu::tick(bus_interface &bus, bool interrupt_requested)
{
    std::uint64_t pins = impl_->pins;

    if (interrupt_requested)
        pins |= Z80_INT;
    else
        pins &= ~static_cast<std::uint64_t>(Z80_INT);

    pins = z80_tick(&impl_->core, pins);

    const bool mreq = (pins & Z80_MREQ) != 0;
    const bool iorq = (pins & Z80_IORQ) != 0;
    const bool refresh = (pins & Z80_RFSH) != 0;
    const bool first_cycle = (pins & Z80_M1) != 0;
    const bool active = mreq || iorq;
    const bool instruction_boundary =
        first_cycle && z80_opdone(&impl_->core);

    int stall = 0;

    // a machine cycle is serviced exactly once, on the T-state its
    // address first appears. the core drops all control pins at the
    // start of every tick, so this really is an edge.
    if (active && !impl_->bus_active) {
        const auto address = static_cast<u16>(Z80_GET_ADDR(pins));

        if (instruction_boundary)
            impl_->finish_instruction();

        if (mreq && !refresh) {
            if (pins & Z80_RD) {
                const u8 value = first_cycle ? bus.opcode_fetch(address)
                                             : bus.memory_read(address);
                Z80_SET_DATA(pins, value);

                if (first_cycle) {
                    if (instruction_boundary)
                        impl_->begin_instruction(value);
                    else
                        impl_->observe_opcode(value);
                }
            } else if (pins & Z80_WR) {
                bus.memory_write(address,
                                 static_cast<u8>(Z80_GET_DATA(pins)));
            }

            stall = bus.memory_wait_states(
                address, first_cycle ? opcode_fetch_lag : memory_lag);
        } else if (iorq) {
            if (first_cycle) {
                // IORQ together with M1 is an interrupt acknowledge,
                // not a port access, and is never contended.
                const u8 value = bus.interrupt_acknowledge();
                Z80_SET_DATA(pins, value);
                impl_->accept_interrupt(value);
            } else if (pins & Z80_RD) {
                Z80_SET_DATA(pins, bus.io_read(address));
                stall = bus.io_wait_states(address, io_read_lag);
            } else if (pins & Z80_WR) {
                bus.io_write(address,
                             static_cast<u8>(Z80_GET_DATA(pins)));
                stall = bus.io_wait_states(address, io_write_lag);
            }
        }

        // a refresh cycle falls through untouched. it does assert MREQ,
        // but the ULA does not contend on it, and nothing is
        // transferred.
    }

    impl_->bus_active = active;
    impl_->pins = pins;
    return stall;
}

bool cpu::instruction_complete() const
{
    return z80_opdone(&impl_->core);
}

bool cpu::halted() const
{
    return (impl_->pins & Z80_HALT) != 0;
}

cpu_registers cpu::registers() const
{
    const z80_t &c = impl_->core;

    cpu_registers regs;
    regs.af = c.af;
    regs.bc = c.bc;
    regs.de = c.de;
    regs.hl = c.hl;
    regs.af_alt = c.af2;
    regs.bc_alt = c.bc2;
    regs.de_alt = c.de2;
    regs.hl_alt = c.hl2;
    regs.ix = c.ix;
    regs.iy = c.iy;
    regs.sp = c.sp;
    regs.pc = c.pc;
    regs.wz = c.wz;
    regs.i = c.i;
    regs.r = c.r;
    regs.im = c.im;
    regs.iff1 = c.iff1;
    regs.iff2 = c.iff2;
    regs.halted = halted();
    return regs;
}

void cpu::set_registers(const cpu_registers &regs)
{
    z80_t &c = impl_->core;

    c.af = regs.af;
    c.bc = regs.bc;
    c.de = regs.de;
    c.hl = regs.hl;
    c.af2 = regs.af_alt;
    c.bc2 = regs.bc_alt;
    c.de2 = regs.de_alt;
    c.hl2 = regs.hl_alt;
    c.ix = regs.ix;
    c.iy = regs.iy;
    c.sp = regs.sp;
    c.wz = regs.wz;
    c.i = regs.i;
    c.r = regs.r;
    c.im = regs.im;
    c.iff1 = regs.iff1;
    c.iff2 = regs.iff2;

    // restart the decoder cleanly at the new pc.
    impl_->pins = z80_prefetch(&c, regs.pc);
    impl_->bus_active = false;
    impl_->reset_instruction_state();

    // z80_prefetch leaves the core's own copy of the pin state alone,
    // and instruction_complete() reads it. Clearing it says "not on an
    // instruction boundary", which is true: the opcode at the new pc
    // has not been fetched yet. Without this the answer would depend on
    // whatever ran before the jump, and a caller stepping straight
    // after setting the registers would see the boundary a whole
    // instruction early.
    impl_->core.pins = 0;
}

void cpu::jump(u16 address)
{
    impl_->pins = z80_prefetch(&impl_->core, address);
    impl_->bus_active = false;
    impl_->core.pins = 0;
    impl_->reset_instruction_state();
}

} // namespace spectrum
