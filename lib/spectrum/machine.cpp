//
// implementation of the machine facade and its master clock.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/machine.h"

namespace spectrum {

namespace {

//
// a run with no limit at all still has to end. one frame is long enough
// to be useful and short enough that a wedged program cannot stall the
// server.
//
constexpr u64 default_tstate_budget_frames = 1;

struct reason_name {
    stop_reason reason;
    const char *name;
};

constexpr reason_name reason_names[] = {
    {stop_reason::completed, "completed"},
    {stop_reason::breakpoint, "breakpoint"},
    {stop_reason::halted, "halted"},
    {stop_reason::address_reached, "address_reached"},
    {stop_reason::step_limit, "step_limit"},
};

} // namespace

const char *stop_reason_name(stop_reason reason)
{
    for (const reason_name &entry : reason_names) {
        if (entry.reason == reason)
            return entry.name;
    }
    return "completed";
}

machine::machine(const machine_timing &timing)
    : timing_(timing),
      frame_length_(static_cast<u32>(timing.tstates_per_frame())),
      contention_(std::make_unique<ula_contention>(timing)),
      ula_(timing, memory_, keyboard_)
{
    ports_.attach(ula_);

    // an unclaimed read picks up whatever the ULA is fetching.
    ports_.set_fallback_reader([this](u16, u32 frame_tstate) {
        return ula_.floating_bus(frame_tstate);
    });

    reset(true);
}

machine::~machine() = default;

void machine::reset(bool clear_memory)
{
    if (clear_memory)
        memory_.reset();

    keyboard_.reset();
    ula_.reset();
    tape_.reset();
    ula_.set_ear_input(tape_.ear_level());
    if (serial_attached_)
        serial_.reset();
    cpu_.reset();

    total_tstates_ = 0;
    frame_tstate_ = 0;
    frame_number_ = 0;
    instructions_ = 0;
    instruction_address_ = 0;
    pending_breakpoint_ = -1;
}

void machine::advance_clock(int tstates)
{
    for (int i = 0; i < tstates; ++i) {
        ula_.tick(frame_tstate_);

        ++frame_tstate_;
        ++total_tstates_;

        tape_.tick();
        ula_.set_ear_input(tape_.ear_level());

        if (serial_attached_)
            serial_.tick(total_tstates_);

        if (frame_tstate_ >= frame_length_) {
            frame_tstate_ = 0;
            ++frame_number_;
            if (frame_observer_)
                frame_observer_(ula_.screen(), frame_number_);
            ula_.begin_frame();
        }
    }
}

void machine::tick_once()
{
    const bool interrupt = ula_.interrupt_active(frame_tstate_);

    // the CPU runs for one T-state and tells us how long the bus held
    // it off. it is not ticked again during the stall, so the ULA runs
    // on alone, exactly as it does while /WAIT is asserted.
    const int stall = cpu_.tick(*this, interrupt);
    advance_clock(1 + stall);
}

u32 machine::cycle_tstate(int tstates_since_start) const
{
    const auto lag = static_cast<u32>(tstates_since_start);
    return (frame_tstate_ + frame_length_ - lag % frame_length_) %
           frame_length_;
}

run_result machine::run(const run_limits &limits)
{
    const u64 start_tstates = total_tstates_;
    const u64 start_instructions = instructions_;
    const u64 start_frames = frame_number_;

    u64 tstate_budget = limits.max_tstates;
    if (limits.max_tstates == 0 && limits.max_instructions == 0 &&
        limits.max_frames == 0 && !limits.until_pc &&
        !limits.stop_on_halt) {
        tstate_budget = static_cast<u64>(frame_length_) *
                        default_tstate_budget_frames;
    }

    run_result result;
    result.reason = stop_reason::completed;
    pending_breakpoint_ = -1;

    // the core overlaps the opcode fetch of the next instruction with
    // the last T-state of the current one, so an instruction boundary
    // is the T-state on which that fetch happens. measuring from one
    // boundary to the next therefore yields exactly the instruction's
    // published T-state count.
    //
    // the first boundary a run meets is the opening one: it marks the
    // instruction this run is about to execute, not one it has just
    // retired, so nothing is counted and no stop condition is judged
    // there. skipping the checks is also what lets execution resume
    // from an address that has a breakpoint on it.
    bool opening_boundary_seen = false;
    u64 retired = 0;

    while (true) {
        if (cpu_.instruction_complete()) {
            if (!opening_boundary_seen) {
                opening_boundary_seen = true;
            } else {
                ++retired;
                ++instructions_;

                const u16 pc = instruction_address_;

                if (pending_breakpoint_ >= 0) {
                    result.reason = stop_reason::breakpoint;
                    result.breakpoint_id = pending_breakpoint_;
                    break;
                }

                if (breakpoint *hit = breakpoints_.match(
                        breakpoint_kind::execute, pc, 0)) {
                    result.reason = stop_reason::breakpoint;
                    result.breakpoint_id = hit->id;
                    break;
                }

                if (limits.until_pc && pc == *limits.until_pc) {
                    result.reason = stop_reason::address_reached;
                    break;
                }

                if (limits.stop_on_halt && cpu_.halted()) {
                    result.reason = stop_reason::halted;
                    break;
                }

                if (limits.max_instructions != 0 &&
                    retired >= limits.max_instructions) {
                    result.reason = stop_reason::step_limit;
                    break;
                }
            }
        }

        if (limits.max_frames != 0 &&
            (frame_number_ - start_frames) >= limits.max_frames) {
            result.reason = stop_reason::completed;
            break;
        }

        if (tstate_budget != 0 &&
            (total_tstates_ - start_tstates) >= tstate_budget) {
            result.reason = stop_reason::completed;
            break;
        }

        tick_once();
    }

    result.tstates = total_tstates_ - start_tstates;
    result.instructions = instructions_ - start_instructions;
    result.frames = frame_number_ - start_frames;
    result.pc = instruction_address_;
    return result;
}

run_result machine::run_tstates(u64 count)
{
    run_limits limits;
    limits.max_tstates = count;
    return run(limits);
}

run_result machine::run_frames(u64 count)
{
    run_limits limits;
    limits.max_frames = count;
    return run(limits);
}

run_result machine::step(u64 instructions)
{
    run_limits limits;
    limits.max_instructions = instructions;
    return run(limits);
}

run_result machine::run_until(u16 address, u64 max_tstates)
{
    run_limits limits;
    limits.until_pc = address;
    limits.max_tstates = max_tstates;
    return run(limits);
}

void machine::note_breakpoint(breakpoint_kind kind, u16 address, u8 value)
{
    if (pending_breakpoint_ >= 0 || !breakpoints_.armed(kind))
        return;

    if (breakpoint *hit = breakpoints_.match(kind, address, value))
        pending_breakpoint_ = hit->id;
}

u8 machine::opcode_fetch(u16 address)
{
    // remember where the instruction stream is. this is what
    // registers() reports as pc and what execution breakpoints are
    // compared against; the raw pc in the core cannot serve, because
    // the overlapped fetch has already advanced it past this opcode.
    //
    // only a fetch that starts a new instruction counts. the second
    // opcode byte of a DD, FD, ED or CB prefixed instruction arrives
    // here too, and recording it would report the middle of an
    // instruction as the program counter whenever a run stopped on a
    // T-state or frame limit rather than on a boundary. the core
    // distinguishes the two for us: instruction_complete() is false
    // while a prefix is being resolved.
    if (cpu_.instruction_complete())
        instruction_address_ = address;

    // Interface 1 asserts ROMCS before the opcode fetch at either hardware
    // trap address. Its UNPAGE routine at $0700 is itself fetched from the
    // shadow ROM, then releases ROMCS for the following access.
    if (serial_attached_ && serial_.has_shadow_rom() &&
        (address == 0x0008 || address == 0x1708)) {
        serial_.page_shadow_rom();
    }

    const u8 value = mapped_memory_read(address);
    if (serial_attached_ && serial_.shadow_rom_paged() &&
        address == 0x0700) {
        serial_.unpage_shadow_rom();
    }
    return value;
}

u8 machine::memory_read(u16 address)
{
    const u8 value = mapped_memory_read(address);
    note_breakpoint(breakpoint_kind::memory_read, address, value);
    return value;
}

void machine::memory_write(u16 address, u8 value)
{
    note_breakpoint(breakpoint_kind::memory_write, address, value);
    memory_.write(address, value);
}

u8 machine::io_read(u16 port)
{
    ula_.set_ear_input(tape_.ear_level());
    const u8 value = ports_.read(port, frame_tstate_);
    note_breakpoint(breakpoint_kind::io_read, port, value);
    return value;
}

void machine::io_write(u16 port, u8 value)
{
    note_breakpoint(breakpoint_kind::io_write, port, value);
    ports_.write(port, value, frame_tstate_);
}

u8 machine::interrupt_acknowledge()
{
    // nothing on an unexpanded 48K drives the bus during the
    // acknowledge, so it floats high.
    return 0xff;
}

int machine::memory_wait_states(u16 address, int tstates_since_start)
{
    return contention_->memory_delay(cycle_tstate(tstates_since_start),
                                     address);
}

int machine::io_wait_states(u16 port, int tstates_since_start)
{
    return contention_->io_delay(cycle_tstate(tstates_since_start), port);
}

u8 machine::read_port(u16 port)
{
    ula_.set_ear_input(tape_.ear_level());
    return ports_.read(port, frame_tstate_);
}

void machine::write_port(u16 port, u8 value)
{
    ports_.write(port, value, frame_tstate_);
}

memory &machine::mem() { return memory_; }

const memory &machine::mem() const { return memory_; }

ula &machine::video() { return ula_; }

const ula &machine::video() const { return ula_; }

keyboard &machine::keys() { return keyboard_; }

const keyboard &machine::keys() const { return keyboard_; }

cpu &machine::processor() { return cpu_; }

const cpu &machine::processor() const { return cpu_; }

cpu_registers machine::registers() const
{
    cpu_registers regs = cpu_.registers();
    regs.pc = instruction_address_;
    return regs;
}

u16 machine::instruction_address() const { return instruction_address_; }

io_bus &machine::ports() { return ports_; }

void machine::enable_interface1_serial()
{
    if (serial_attached_)
        return;
    ports_.attach(serial_);
    serial_attached_ = true;
    serial_.reset();
}

bool machine::load_interface1_rom(std::span<const u8> data,
                                  std::string &error)
{
    if (!serial_.load_shadow_rom(data, error))
        return false;
    enable_interface1_serial();
    return true;
}

bool machine::enable_serial_bridge(const serial_bridge_config &config,
                                   std::string &error)
{
    if (!serial_.start_bridge(config, error))
        return false;
    if (!serial_attached_) {
        ports_.attach(serial_);
        serial_attached_ = true;
    }
    return true;
}

bool machine::interface1_serial_enabled() const
{
    return serial_attached_;
}

interface1_serial &machine::serial() { return serial_; }

const interface1_serial &machine::serial() const { return serial_; }

tape_deck &machine::tape() { return tape_; }

const tape_deck &machine::tape() const { return tape_; }

u8 machine::mapped_memory_read(u16 address) const
{
    if (serial_attached_ && serial_.shadow_rom_paged() &&
        address < memory::rom_size) {
        return serial_.read_shadow_rom(address);
    }
    return memory_.read(address);
}

breakpoint_set &machine::breakpoints() { return breakpoints_; }

const breakpoint_set &machine::breakpoints() const { return breakpoints_; }

const machine_timing &machine::timing() const { return timing_; }

const contention_model &machine::contention() const { return *contention_; }

void machine::set_contention(std::unique_ptr<contention_model> model)
{
    if (model)
        contention_ = std::move(model);
}

u64 machine::total_tstates() const { return total_tstates_; }

u32 machine::frame_tstate() const { return frame_tstate_; }

u64 machine::frame_number() const { return frame_number_; }

void machine::set_frame_observer(frame_observer observer)
{
    frame_observer_ = std::move(observer);
}

u64 machine::instruction_count() const { return instructions_; }

} // namespace spectrum
