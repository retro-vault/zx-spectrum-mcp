//
// instruction timing, contention timing, and program counter reporting.
//
// this is the test the emulator's claim to be cycle accurate rests on.
// it does three things:
//
// 1. checks published T-state counts with contention switched off, so
//    the CPU core is measured on its own.
// 2. checks that contention lengthens exactly the instructions it
//    should, by exactly the amount the ULA table says, which is what
//    validates the machine cycle timing correction in cpu.cpp. if the
//    correction were wrong these would be off by one or two T-states.
// 3. checks that the reported program counter names the instruction
//    about to run even though the core's own pc has already moved past
//    it, and even when a run stops in the middle of an instruction.
//
// timing is measured from one instruction boundary to the next. the
// core overlaps the opcode fetch of the next instruction with the tail
// of the current one, so that window is exactly the instruction's
// length. jump() leaves the core mid-fetch, hence the single
// synchronising tick before each measurement.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <memory>
#include <vector>

#include "spectrum/machine.h"
#include "test_support.h"

using namespace spectrum;

namespace {

//
// Run one instruction and return how many T-states it took.
//
// Parameters:
//      target      - the machine.
//      code        - the instruction's bytes.
//      at          - where to place and run it.
//
u64 time_instruction(machine &target, const std::vector<u8> &code,
                     u16 at = 0x8000)
{
    target.mem().write_block(at, code, false);
    target.processor().jump(at);
    target.run_tstates(1); // reach the opening instruction boundary

    const u64 before = target.total_tstates();
    target.step(1);
    return target.total_tstates() - before;
}

//
// Run one instruction that begins at a chosen position in the frame.
//
// Parameters:
//      code        - the instruction's bytes.
//      start       - frame T-state at which the instruction's first
//                    T-state should execute.
//
// Returns:
//      how many T-states the instruction took.
//
u64 time_instruction_at(const std::vector<u8> &code, u32 start)
{
    machine target;

    // step to the wanted position. the stub ROM runs from uncontended
    // memory, so nothing here perturbs the count.
    while (target.frame_tstate() != start)
        target.run_tstates(1);

    target.mem().write_block(0x8000, code, false);
    target.processor().jump(0x8000);
    target.run_tstates(1); // the instruction's T1 runs at `start`

    const u64 before = target.total_tstates();
    target.step(1);
    return target.total_tstates() - before;
}

void test_uncontended_timings()
{
    test::section("published instruction timings, contention off");

    machine target;
    target.set_contention(std::make_unique<no_contention>());

    struct sample {
        const char *name;
        std::vector<u8> code;
        int tstates;
    };

    const sample samples[] = {
        {"NOP", {0x00}, 4},
        {"LD BC,nn", {0x01, 0x34, 0x12}, 10},
        {"LD A,(HL)", {0x7e}, 7},
        {"LD (HL),A", {0x77}, 7},
        {"INC B", {0x04}, 4},
        {"INC (HL)", {0x34}, 11},
        {"JP nn", {0xc3, 0x00, 0x90}, 10},
        {"CALL nn", {0xcd, 0x00, 0x90}, 17},
        {"PUSH BC", {0xc5}, 11},
        {"POP BC", {0xc1}, 10},
        {"RET", {0xc9}, 10},
        {"EX AF,AF'", {0x08}, 4},
        {"EXX", {0xd9}, 4},
        {"IN A,(n)", {0xdb, 0xfe}, 11},
        {"OUT (n),A", {0xd3, 0xfe}, 11},
        {"IN A,(C)", {0xed, 0x78}, 12},
        {"OUT (C),B", {0xed, 0x41}, 12},
        {"LDIR, one pass", {0xed, 0xb0}, 21},
        {"DJNZ taken", {0x10, 0xfe}, 13},
        {"BIT 0,(HL)", {0xcb, 0x46}, 12},
        {"LD A,(IX+d)", {0xdd, 0x7e, 0x00}, 19},
        {"ADD HL,BC", {0x09}, 11},
        {"LD A,(nn)", {0x3a, 0x00, 0x60}, 13},
        {"LD (nn),HL", {0x22, 0x00, 0x60}, 16},
        {"RST 8", {0xcf}, 11},
    };

    for (const sample &item : samples) {
        test::check_eq(
            static_cast<long long>(time_instruction(target, item.code)),
            item.tstates, item.name);
    }
}

void test_contention_is_applied()
{
    test::section("ULA contention applied to real instructions");

    // LD A,(nn) is 13 T-states as 4 + 3 + 3 + 3. Its final machine
    // cycle, the one that reads the operand address, therefore begins
    // 10 T-states after the instruction starts. Placing the code in
    // uncontended RAM leaves that read as the only cycle the ULA can
    // interfere with, so the whole instruction lengthens by exactly the
    // table entry for the T-state that read begins on.
    const std::vector<u8> read_contended = {0x3a, 0x00, 0x40};
    const std::vector<u8> read_free = {0x3a, 0x00, 0x80};

    const ula_contention table(zx_spectrum_48k_timing);

    struct probe {
        const char *name;
        u32 cycle_start;
    };

    const probe probes[] = {
        {"first contended T-state", 14335},
        {"second block position", 14336},
        {"late in a block", 14340},
        {"free tail of a block", 14341},
        {"second free tail T-state", 14342},
        {"start of the next block", 14343},
        {"middle of a display line", 14335 + 64},
    };

    for (const probe &item : probes) {
        const u32 instruction_start = item.cycle_start - 10;
        const int expected = table.raw_delay(item.cycle_start);

        test::check_eq(
            static_cast<long long>(
                time_instruction_at(read_contended, instruction_start)),
            13 + expected,
            std::string("LD A,(0x4000) ") + item.name + ": 13+" +
                std::to_string(expected));
    }

    test::section("contention only where it belongs");

    // uncontended target memory is never delayed, wherever the beam is.
    test::check_eq(
        static_cast<long long>(time_instruction_at(read_free, 14325)), 13,
        "LD A,(0x8000) during the display is never delayed");

    // and contended memory is free while the beam is off the display.
    test::check_eq(
        static_cast<long long>(time_instruction_at(read_contended, 100)),
        13, "LD A,(0x4000) during vertical blanking is not delayed");

    test::check_eq(
        static_cast<long long>(
            time_instruction_at(read_contended, 14335 + 130 - 10)),
        13, "LD A,(0x4000) during the right border is not delayed");
}

void test_program_counter_reporting()
{
    test::section("program counter reporting");

    machine target;
    target.set_contention(std::make_unique<no_contention>());

    // LD BC,0x1234 followed by NOP.
    target.mem().write_block(0x8000,
                             std::vector<u8>{0x01, 0x34, 0x12, 0x00},
                             false);
    target.processor().jump(0x8000);
    target.run_tstates(1);

    test::check_eq(target.registers().pc, 0x8000,
                   "pc names the instruction about to run");

    target.step(1);
    test::check_eq(target.registers().pc, 0x8003,
                   "pc advances by the instruction length");
    test::check_eq(target.registers().bc, 0x1234, "BC was loaded");

    // A prefixed instruction fetches two opcode bytes. Stopping part
    // way through one must still report the address of the instruction
    // itself, not of its second byte.
    machine prefixed;
    prefixed.set_contention(std::make_unique<no_contention>());
    prefixed.mem().write_block(0x9000,
                               std::vector<u8>{0xed, 0xb0, 0x00}, false);
    prefixed.processor().jump(0x9000);
    prefixed.run_tstates(1);
    test::check_eq(prefixed.registers().pc, 0x9000,
                   "pc points at ED, not at the byte after it");

    // stop deliberately inside the instruction.
    prefixed.run_tstates(6);
    test::check_eq(prefixed.registers().pc, 0x9000,
                   "pc still names the instruction when stopped inside "
                   "it");
}

void test_halt_and_interrupts()
{
    test::section("halt and the frame interrupt");

    machine target;

    // DI, HALT: with interrupts off the CPU stays halted forever, so a
    // frame limited run must still return.
    target.mem().write_block(0x8000, std::vector<u8>{0xf3, 0x76}, false);
    target.processor().jump(0x8000);

    run_limits limits;
    limits.max_frames = 2;
    const run_result result = target.run(limits);

    test::check(target.processor().halted(),
                "the CPU is halted after HALT with interrupts disabled");
    test::check_eq(static_cast<long long>(result.frames), 2,
                   "the run still finished its two frames");

    // EI, HALT in interrupt mode 1: the frame interrupt wakes it and
    // vectors to 0x0038.
    machine woken;
    woken.mem().write_block(
        0x8000, std::vector<u8>{0xed, 0x56, 0xfb, 0x76}, false);
    woken.mem().write_block(0x0038, std::vector<u8>{0xfb, 0xc9}, true);
    woken.processor().jump(0x8000);

    run_limits wake;
    wake.max_frames = 3;
    woken.run(wake);

    test::check(woken.instruction_count() > 4,
                "the frame interrupt woke the CPU out of HALT");
}

} // namespace

int main()
{
    test_uncontended_timings();
    test_contention_is_applied();
    test_program_counter_reporting();
    test_halt_and_interrupts();
    return test::summary("cpu timing");
}
