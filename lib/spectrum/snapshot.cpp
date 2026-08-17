//
// format dispatch and the simple loaders: binary, .scr and .sna.
//
// the .z80 loader lives in snapshot_z80.cpp, which is long enough on
// its own.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/snapshot.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace spectrum {

namespace {

struct format_name {
    snapshot_format format;
    const char *name;
};

constexpr format_name format_names[] = {
    {snapshot_format::binary, "binary"},
    {snapshot_format::scr, "scr"},
    {snapshot_format::sna, "sna"},
    {snapshot_format::z80, "z80"},
    {snapshot_format::tap, "tap"},
    {snapshot_format::tzx, "tzx"},
};

// a 48K .sna is a fixed 27 byte header followed by all of RAM.
constexpr std::size_t sna_header_size = 27;
constexpr std::size_t sna_total_size = sna_header_size + 49152;

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    });
    return out;
}

load_result failure(std::string message)
{
    load_result result;
    result.ok = false;
    result.error = std::move(message);
    return result;
}

//
// load a raw block of bytes at a chosen address.
//
load_result load_binary(machine &target, std::span<const u8> data,
                        const load_options &options)
{
    if (data.empty())
        return failure("image is empty");

    if (data.size() > memory::address_space)
        return failure("image is larger than the 64K address space");

    const std::size_t written =
        target.mem().write_block(options.address, data, false);

    load_result result;
    result.ok = true;
    result.bytes = data.size();
    result.entry = options.start.value_or(options.address);

    // only the program counter moves; registers and RAM outside the
    // loaded range are left as they were, so several images can be
    // loaded before starting one of them.
    if (options.start)
        target.processor().jump(*options.start);

    char buffer[160];
    std::snprintf(buffer, sizeof buffer,
                  "loaded %zu bytes at 0x%04X (%zu written, %zu fell in "
                  "ROM and were skipped)",
                  data.size(), options.address, written,
                  data.size() - written);
    result.description = buffer;
    return result;
}

//
// load a display file dump straight into video memory.
//
load_result load_scr(machine &target, std::span<const u8> data)
{
    if (data.size() < memory::screen_bytes)
        return failure("a .scr image must be at least 6912 bytes");

    target.mem().write_block(memory::screen_base,
                             data.first(memory::screen_bytes), false);

    load_result result;
    result.ok = true;
    result.bytes = memory::screen_bytes;
    result.entry = target.instruction_address();
    result.description =
        "loaded a 6912 byte display file; the CPU was not disturbed";
    return result;
}

//
// load a 48K machine state.
//
load_result load_sna(machine &target, std::span<const u8> data)
{
    if (data.size() < sna_total_size)
        return failure("a 48K .sna image must be at least 49179 bytes");

    target.reset(true);

    const auto word = [&data](std::size_t offset) {
        return static_cast<u16>(data[offset] |
                                (data[offset + 1] << 8));
    };

    cpu_registers regs;
    regs.i = data[0];
    regs.hl_alt = word(1);
    regs.de_alt = word(3);
    regs.bc_alt = word(5);
    regs.af_alt = word(7);
    regs.hl = word(9);
    regs.de = word(11);
    regs.bc = word(13);
    regs.iy = word(15);
    regs.ix = word(17);

    // the format only records IFF2; both flip flops are restored from
    // it, which is what every other emulator does.
    const bool interrupts_enabled = (data[19] & 0x04) != 0;
    regs.iff1 = interrupts_enabled;
    regs.iff2 = interrupts_enabled;

    regs.r = data[20];
    regs.af = word(21);
    regs.sp = word(23);
    regs.im = static_cast<u8>(data[25] & 0x03);

    target.video().set_border(static_cast<u8>(data[26] & 0x07));

    target.mem().write_block(
        memory::screen_base,
        data.subspan(sna_header_size, 49152), false);

    // a .sna has no program counter field. it was saved with a
    // maskable interrupt, so the return address is still on the stack;
    // popping it is how the state resumes.
    const u16 stack = regs.sp;
    const auto low = target.mem().read(stack);
    const auto high = target.mem().read(static_cast<u16>(stack + 1));
    regs.pc = static_cast<u16>(low | (high << 8));
    regs.sp = static_cast<u16>(stack + 2);

    target.processor().set_registers(regs);

    load_result result;
    result.ok = true;
    result.bytes = sna_total_size;
    result.entry = regs.pc;

    char buffer[160];
    std::snprintf(buffer, sizeof buffer,
                  "restored a 48K .sna state, resuming at 0x%04X "
                  "(popped from the stack)",
                  regs.pc);
    result.description = buffer;
    return result;
}

load_result load_tape(machine &target, std::span<const u8> data,
                      snapshot_format format, bool autoplay)
{
    std::string error;
    const bool ok = format == snapshot_format::tap
                        ? target.tape().insert_tap(data, autoplay, error)
                        : target.tape().insert_tzx(data, autoplay, error);
    if (!ok)
        return failure(error);

    const tape_status tape = target.tape().status();
    load_result result;
    result.ok = true;
    result.bytes = data.size();
    result.entry = target.instruction_address();
    result.description = "inserted " + std::string(tape.format == "tap"
                                                        ? "TAP"
                                                        : "TZX") +
                         " tape image with " +
                         std::to_string(tape.blocks) + " blocks" +
                         (autoplay ? "; playback started"
                                   : "; playback is stopped");
    return result;
}

} // namespace

const char *snapshot_format_name(snapshot_format format)
{
    for (const format_name &entry : format_names) {
        if (entry.format == format)
            return entry.name;
    }
    return "binary";
}

std::optional<snapshot_format> snapshot_format_from_name(
    std::string_view name)
{
    const std::string wanted = lower(name);
    for (const format_name &entry : format_names) {
        if (wanted == entry.name)
            return entry.format;
    }

    // a couple of spellings people reach for.
    if (wanted == "bin" || wanted == "raw")
        return snapshot_format::binary;
    if (wanted == "screen")
        return snapshot_format::scr;

    return std::nullopt;
}

std::optional<snapshot_format> snapshot_format_from_path(
    std::string_view path)
{
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos)
        return std::nullopt;

    return snapshot_format_from_name(path.substr(dot + 1));
}

std::optional<std::vector<u8>> read_file(const std::string &path,
                                         std::string &error)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = "cannot open '" + path + "'";
        return std::nullopt;
    }

    std::vector<u8> contents;
    u8 buffer[8192];

    while (true) {
        const std::size_t got =
            std::fread(buffer, 1, sizeof buffer, file);
        contents.insert(contents.end(), buffer, buffer + got);
        if (got < sizeof buffer)
            break;
    }

    const bool failed = std::ferror(file) != 0;
    std::fclose(file);

    if (failed) {
        error = "error while reading '" + path + "'";
        return std::nullopt;
    }

    return contents;
}

std::vector<u8> decompress_z80_block(std::span<const u8> source,
                                     bool stop_at_end)
{
    std::vector<u8> out;
    std::size_t i = 0;

    while (i < source.size()) {
        // version 1 files finish with this marker rather than a length.
        if (stop_at_end && i + 4 <= source.size() && source[i] == 0x00 &&
            source[i + 1] == 0xed && source[i + 2] == 0xed &&
            source[i + 3] == 0x00)
            break;

        // ED ED count value expands to a run. a single ED is literal,
        // which is why the pair is required.
        if (i + 4 <= source.size() && source[i] == 0xed &&
            source[i + 1] == 0xed) {
            const auto count = static_cast<std::size_t>(source[i + 2]);
            const u8 value = source[i + 3];
            out.insert(out.end(), count, value);
            i += 4;
            continue;
        }

        out.push_back(source[i]);
        ++i;
    }

    return out;
}

load_result load_into(machine &target, std::span<const u8> data,
                      const load_options &options)
{
    if (options.reset_first)
        target.reset(true);

    switch (options.format) {
    case snapshot_format::binary:
        return load_binary(target, data, options);
    case snapshot_format::scr:
        return load_scr(target, data);
    case snapshot_format::sna:
        return load_sna(target, data);
    case snapshot_format::z80:
        return load_z80_snapshot(target, data);
    case snapshot_format::tap:
    case snapshot_format::tzx:
        return load_tape(target, data, options.format, options.autoplay);
    }

    return failure("unknown format");
}

} // namespace spectrum
