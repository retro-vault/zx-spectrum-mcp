//
// External Z80 conformance vectors from the Fuse ZX Spectrum emulator.
//
// The fixtures under data/fuse-z80 are unmodified upstream files. This
// adapter deliberately drives spectrum::cpu through bus_interface rather
// than including the vendored z80.h, so it tests the same wrapper and bus
// path used by the Spectrum machine.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "spectrum/cpu.h"
#include "test_support.h"

using namespace spectrum;

namespace {

struct memory_patch {
    u16 address = 0;
    std::vector<u8> bytes;
};

struct test_vector {
    std::string name;
    cpu_registers regs;
    u64 tstates = 0;
    std::vector<memory_patch> memory;
};

std::string trim(const std::string &value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

unsigned parse_hex(const std::string &token)
{
    std::size_t consumed = 0;
    const unsigned value =
        static_cast<unsigned>(std::stoul(token, &consumed, 16));
    if (consumed != token.size())
        throw std::runtime_error("invalid hexadecimal token: " + token);
    return value;
}

cpu_registers parse_registers(const std::string &line)
{
    std::istringstream fields(line);
    std::array<std::string, 13> values;
    for (std::string &value : values) {
        if (!(fields >> value))
            throw std::runtime_error("short register line: " + line);
    }

    std::string extra;
    if (fields >> extra)
        throw std::runtime_error("long register line: " + line);

    cpu_registers regs;
    regs.af = static_cast<u16>(parse_hex(values[0]));
    regs.bc = static_cast<u16>(parse_hex(values[1]));
    regs.de = static_cast<u16>(parse_hex(values[2]));
    regs.hl = static_cast<u16>(parse_hex(values[3]));
    regs.af_alt = static_cast<u16>(parse_hex(values[4]));
    regs.bc_alt = static_cast<u16>(parse_hex(values[5]));
    regs.de_alt = static_cast<u16>(parse_hex(values[6]));
    regs.hl_alt = static_cast<u16>(parse_hex(values[7]));
    regs.ix = static_cast<u16>(parse_hex(values[8]));
    regs.iy = static_cast<u16>(parse_hex(values[9]));
    regs.sp = static_cast<u16>(parse_hex(values[10]));
    regs.pc = static_cast<u16>(parse_hex(values[11]));
    regs.wz = static_cast<u16>(parse_hex(values[12]));
    return regs;
}

void parse_status(const std::string &line, test_vector &item)
{
    std::istringstream fields(line);
    std::string i;
    std::string r;
    unsigned iff1 = 0;
    unsigned iff2 = 0;
    unsigned im = 0;
    unsigned halted = 0;

    if (!(fields >> i >> r >> iff1 >> iff2 >> im >> halted >>
          item.tstates)) {
        throw std::runtime_error("bad CPU status line: " + line);
    }

    item.regs.i = static_cast<u8>(parse_hex(i));
    item.regs.r = static_cast<u8>(parse_hex(r));
    item.regs.iff1 = iff1 != 0;
    item.regs.iff2 = iff2 != 0;
    item.regs.im = static_cast<u8>(im);
    item.regs.halted = halted != 0;
}

memory_patch parse_memory_patch(const std::string &line)
{
    std::istringstream fields(line);
    std::string token;
    if (!(fields >> token) || token == "-1")
        throw std::runtime_error("bad memory line: " + line);

    memory_patch patch;
    patch.address = static_cast<u16>(parse_hex(token));

    bool terminated = false;
    while (fields >> token) {
        if (token == "-1") {
            terminated = true;
            break;
        }
        patch.bytes.push_back(static_cast<u8>(parse_hex(token)));
    }

    if (!terminated)
        throw std::runtime_error("unterminated memory line: " + line);
    return patch;
}

bool is_event_line(const std::string &line)
{
    std::istringstream fields(line);
    std::string time;
    std::string kind;
    if (!(fields >> time >> kind))
        return false;
    return kind == "MR" || kind == "MW" || kind == "MC" ||
           kind == "PR" || kind == "PW" || kind == "PC";
}

std::vector<test_vector> read_vectors(const std::filesystem::path &path,
                                      bool expected)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());

    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty()) {
            if (!record.empty()) {
                records.push_back(std::move(record));
                record.clear();
            }
            continue;
        }
        record.push_back(line);
    }
    if (!record.empty())
        records.push_back(std::move(record));

    std::vector<test_vector> vectors;
    for (const std::vector<std::string> &lines : records) {
        std::size_t index = 0;
        test_vector item;
        item.name = lines.at(index++);

        if (expected)
            while (index < lines.size() && is_event_line(lines[index]))
                ++index;

        if (index >= lines.size())
            throw std::runtime_error("missing registers for " + item.name);
        item.regs = parse_registers(lines[index++]);
        if (index >= lines.size())
            throw std::runtime_error("missing CPU status for " + item.name);
        parse_status(lines[index++], item);

        bool terminated = expected;
        while (index < lines.size()) {
            if (lines[index] == "-1") {
                terminated = true;
                ++index;
                break;
            }
            item.memory.push_back(parse_memory_patch(lines[index++]));
        }
        if (!terminated)
            throw std::runtime_error("missing memory terminator for " +
                                     item.name);
        if (index != lines.size())
            throw std::runtime_error("data follows memory terminator for " +
                                     item.name);

        vectors.push_back(std::move(item));
    }
    return vectors;
}

class flat_bus final : public bus_interface {
public:
    explicit flat_bus(cpu &processor) : processor_(processor)
    {
        static constexpr std::array<u8, 4> pattern = {
            0xde, 0xad, 0xbe, 0xef};
        for (std::size_t i = 0; i < memory_.size(); ++i)
            memory_[i] = pattern[i % pattern.size()];
    }

    u8 opcode_fetch(u16 address) override
    {
        if (processor_.instruction_complete())
            instruction_address_ = address;
        return memory_[address];
    }

    u8 memory_read(u16 address) override { return memory_[address]; }

    void memory_write(u16 address, u8 value) override
    {
        memory_[address] = value;
    }

    // Fuse's core-test harness returns the high byte of the port. This
    // deterministic peripheral is part of the published vector contract.
    u8 io_read(u16 port) override { return static_cast<u8>(port >> 8); }

    void io_write(u16, u8) override {}

    u8 interrupt_acknowledge() override { return 0xff; }

    int memory_wait_states(u16, int) override { return 0; }

    int io_wait_states(u16, int) override { return 0; }

    void apply(const std::vector<memory_patch> &patches)
    {
        for (const memory_patch &patch : patches) {
            u16 address = patch.address;
            for (u8 value : patch.bytes) {
                memory_[address] = value;
                ++address;
            }
        }
    }

    const std::array<u8, 65536> &memory() const { return memory_; }

    u16 instruction_address() const { return instruction_address_; }

private:
    cpu &processor_;
    std::array<u8, 65536> memory_{};
    u16 instruction_address_ = 0;
};

std::string hex_value(unsigned value, int width)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(width)
        << value;
    return out.str();
}

void compare_register(std::vector<std::string> &errors,
                      const char *name, unsigned got, unsigned want,
                      int width = 4)
{
    if (got != want) {
        errors.push_back(std::string(name) + "=" + hex_value(got, width) +
                         ", expected " + hex_value(want, width));
    }
}

std::string join_errors(const std::vector<std::string> &errors)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i != 0)
            out << "; ";
        out << errors[i];
    }
    return out.str();
}

std::string run_vector(const test_vector &input,
                       const test_vector &expected)
{
    cpu processor;
    flat_bus bus(processor);
    bus.apply(input.memory);

    std::array<u8, 65536> expected_memory = bus.memory();
    for (const memory_patch &patch : expected.memory) {
        u16 address = patch.address;
        for (u8 value : patch.bytes) {
            expected_memory[address] = value;
            ++address;
        }
    }

    processor.set_registers(input.regs);

    // set_registers() leaves the tick-stepped core immediately before
    // the opening opcode fetch. Reach that boundary first, then measure
    // boundary-to-boundary just as Fuse's instruction-stepped harness
    // does. The synchronising tick is not part of the instruction time.
    if (processor.tick(bus, false) != 0)
        return "flat bus unexpectedly inserted wait states";

    u64 elapsed = 0;
    do {
        const int wait = processor.tick(bus, false);
        if (wait != 0)
            return "flat bus unexpectedly inserted wait states";
        ++elapsed;
    } while (elapsed < input.tstates || !processor.instruction_complete());

    cpu_registers got = processor.registers();
    got.pc = bus.instruction_address();
    got.halted = processor.halted();

    std::vector<std::string> errors;
    compare_register(errors, "AF", got.af, expected.regs.af);
    compare_register(errors, "BC", got.bc, expected.regs.bc);
    compare_register(errors, "DE", got.de, expected.regs.de);
    compare_register(errors, "HL", got.hl, expected.regs.hl);
    compare_register(errors, "AF'", got.af_alt, expected.regs.af_alt);
    compare_register(errors, "BC'", got.bc_alt, expected.regs.bc_alt);
    compare_register(errors, "DE'", got.de_alt, expected.regs.de_alt);
    compare_register(errors, "HL'", got.hl_alt, expected.regs.hl_alt);
    compare_register(errors, "IX", got.ix, expected.regs.ix);
    compare_register(errors, "IY", got.iy, expected.regs.iy);
    compare_register(errors, "SP", got.sp, expected.regs.sp);
    compare_register(errors, "PC", got.pc, expected.regs.pc);
    compare_register(errors, "WZ", got.wz, expected.regs.wz);
    compare_register(errors, "I", got.i, expected.regs.i, 2);
    compare_register(errors, "R", got.r, expected.regs.r, 2);
    compare_register(errors, "IM", got.im, expected.regs.im, 1);

    if (got.iff1 != expected.regs.iff1)
        errors.push_back("IFF1 differs");
    if (got.iff2 != expected.regs.iff2)
        errors.push_back("IFF2 differs");
    if (got.halted != expected.regs.halted)
        errors.push_back("HALT state differs");
    if (elapsed != expected.tstates) {
        errors.push_back("timing=" + std::to_string(elapsed) +
                         ", expected " +
                         std::to_string(expected.tstates));
    }

    if (bus.memory() != expected_memory) {
        for (std::size_t i = 0; i < expected_memory.size(); ++i) {
            if (bus.memory()[i] != expected_memory[i]) {
                errors.push_back(
                    "memory[" + hex_value(static_cast<unsigned>(i), 4) +
                    "]=" + hex_value(bus.memory()[i], 2) + ", expected " +
                    hex_value(expected_memory[i], 2));
                break;
            }
        }
    }

    return join_errors(errors);
}

cpu_registers run_sequence(const std::vector<u8> &code,
                           const cpu_registers &initial,
                           std::size_t instructions)
{
    cpu processor;
    flat_bus bus(processor);
    bus.apply({memory_patch{0, code}});
    processor.set_registers(initial);

    if (processor.tick(bus, false) != 0)
        throw std::runtime_error("flat bus inserted an opening wait");

    for (std::size_t i = 0; i < instructions; ++i) {
        do {
            if (processor.tick(bus, false) != 0)
                throw std::runtime_error("flat bus inserted a wait");
        } while (!processor.instruction_complete());
    }
    return processor.registers();
}

void test_q_latch_sequences()
{
    test::section("Q latch across instruction sequences");

    constexpr u8 undocumented = 0x28;

    cpu_registers inc;
    inc.bc = 0x2700; // INC B produces both undocumented flag bits.

    cpu_registers got = run_sequence({0x04, 0x37}, inc, 2);
    test::check_eq(got.af & undocumented, 0,
                   "SCF takes bits 3 and 5 from A after INC changes F");

    got = run_sequence({0x04, 0x00, 0x37}, inc, 3);
    test::check_eq(got.af & undocumented, undocumented,
                   "SCF retains old flag bits after an intervening NOP");

    cpu_registers bit;
    bit.bc = 0x2800;
    got = run_sequence({0xcb, 0x40, 0x37}, bit, 2);
    test::check_eq(got.af & undocumented, 0,
                   "a CB BIT instruction is recorded as changing F");

    cpu_registers res;
    res.af = undocumented;
    got = run_sequence({0xcb, 0x80, 0x37}, res, 2);
    test::check_eq(got.af & undocumented, undocumented,
                   "a CB RES instruction is recorded as preserving F");
}

void test_fuse_vectors()
{
    test::section("Fuse Z80 conformance vectors");

    const std::filesystem::path data =
        std::filesystem::path(__FILE__).parent_path() / "data" /
        "fuse-z80";

    const std::vector<test_vector> inputs =
        read_vectors(data / "tests.in", false);
    const std::vector<test_vector> expected =
        read_vectors(data / "tests.expected", true);

    test::check_eq(static_cast<long long>(inputs.size()), 1356,
                   "the complete pinned input corpus is present");
    test::check_eq(static_cast<long long>(expected.size()), 1356,
                   "the complete pinned result corpus is present");

    std::vector<std::string> failures;
    const std::size_t count = std::min(inputs.size(), expected.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (inputs[i].name != expected[i].name) {
            failures.push_back("vector order differs at " +
                               std::to_string(i));
            continue;
        }

        const std::string error = run_vector(inputs[i], expected[i]);
        if (!error.empty() && failures.size() < 20)
            failures.push_back(inputs[i].name + ": " + error);
    }

    test::report(failures.empty(),
                 "all 1,356 register, memory, I/O and timing vectors",
                 failures.empty() ? std::string{} : join_errors(failures));
}

} // namespace

int main()
{
    try {
        test_fuse_vectors();
        test_q_latch_sequences();
    } catch (const std::exception &error) {
        test::report(false, "the Fuse corpus parses", error.what());
    }
    return test::summary("z80 vectors");
}
