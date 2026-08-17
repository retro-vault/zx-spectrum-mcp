//
// loader for .z80 snapshots, versions 1, 2 and 3.
//
// the format grew by accretion and it shows. version 1 is a 30 byte
// header followed by the whole of RAM, optionally run length encoded.
// versions 2 and 3 keep that header but set its program counter field
// to zero as a flag, then add a second header whose length says which
// version this is, then store RAM as a series of numbered pages.
//
// only 48K machines are restored. a 128K snapshot carries pages this
// machine has nowhere to put, so it is rejected with a clear message
// rather than being loaded into a state that would misbehave later.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <cstdio>

#include "spectrum/snapshot.h"

namespace spectrum {

namespace {

constexpr std::size_t v1_header_size = 30;
constexpr std::size_t page_size = 0x4000;

//
// where each numbered page belongs in a 48K address space.
//
// the numbering comes from the 128K layout, and a 48K snapshot borrows
// three of its pages: 4, 5 and 8.
//
u16 page_address(int page)
{
    switch (page) {
    case 4: return 0x8000;
    case 5: return 0xc000;
    case 8: return 0x4000;
    default: return 0;
    }
}

bool page_belongs_to_48k(int page)
{
    return page == 4 || page == 5 || page == 8;
}

load_result failure(std::string message)
{
    load_result result;
    result.ok = false;
    result.error = std::move(message);
    return result;
}

u16 word_at(std::span<const u8> data, std::size_t offset)
{
    return static_cast<u16>(data[offset] | (data[offset + 1] << 8));
}

//
// pull the register file out of the 30 byte version 1 header, which
// every version keeps.
//
cpu_registers read_common_header(std::span<const u8> data)
{
    cpu_registers regs;

    regs.af = static_cast<u16>((data[0] << 8) | data[1]);
    regs.bc = word_at(data, 2);
    regs.hl = word_at(data, 4);
    regs.sp = word_at(data, 8);
    regs.i = data[10];

    // byte 12 is documented as holding bit 7 of R, because byte 11 only
    // ever stored the low seven bits.
    u8 flags1 = data[12];
    if (flags1 == 0xff)
        flags1 = 1; // a documented quirk of some very old files

    regs.r = static_cast<u8>((data[11] & 0x7f) |
                             ((flags1 & 0x01) ? 0x80 : 0x00));

    regs.de = word_at(data, 13);
    regs.bc_alt = word_at(data, 15);
    regs.de_alt = word_at(data, 17);
    regs.hl_alt = word_at(data, 19);
    regs.af_alt = static_cast<u16>((data[21] << 8) | data[22]);
    regs.iy = word_at(data, 23);
    regs.ix = word_at(data, 25);
    regs.iff1 = data[27] != 0;
    regs.iff2 = data[28] != 0;
    regs.im = static_cast<u8>(data[29] & 0x03);

    return regs;
}

u8 border_from_header(std::span<const u8> data)
{
    u8 flags1 = data[12];
    if (flags1 == 0xff)
        flags1 = 1;
    return static_cast<u8>((flags1 >> 1) & 0x07);
}

//
// version 1: one block holding all 48K from 0x4000 upwards.
//
load_result load_v1(machine &target, std::span<const u8> data,
                    const cpu_registers &header_regs)
{
    u8 flags1 = data[12];
    if (flags1 == 0xff)
        flags1 = 1;

    const bool compressed = (flags1 & 0x20) != 0;
    const std::span<const u8> body = data.subspan(v1_header_size);

    std::vector<u8> ram;
    if (compressed)
        ram = decompress_z80_block(body, true);
    else
        ram.assign(body.begin(), body.end());

    if (ram.size() < 3 * page_size)
        ram.resize(3 * page_size, 0);

    target.mem().write_block(0x4000,
                             std::span<const u8>(ram).first(3 * page_size),
                             false);

    cpu_registers regs = header_regs;
    regs.pc = word_at(data, 6);
    target.processor().set_registers(regs);

    load_result result;
    result.ok = true;
    result.bytes = data.size();
    result.entry = regs.pc;

    char buffer[160];
    std::snprintf(buffer, sizeof buffer,
                  "restored a version 1 .z80 snapshot (%s), resuming at "
                  "0x%04X",
                  compressed ? "compressed" : "uncompressed", regs.pc);
    result.description = buffer;
    return result;
}

//
// versions 2 and 3: an extra header, then one block per memory page.
//
load_result load_v2_or_v3(machine &target, std::span<const u8> data,
                          const cpu_registers &header_regs)
{
    if (data.size() < v1_header_size + 2)
        return failure(".z80 image ends inside its extra header length");

    const std::size_t extra_length = word_at(data, v1_header_size);
    const std::size_t body_start = v1_header_size + 2 + extra_length;

    if (data.size() < body_start)
        return failure(".z80 image ends inside its extra header");
    if (extra_length < 3)
        return failure(".z80 extra header is too short to be valid");

    const int version = extra_length == 23 ? 2 : 3;
    const u16 pc = word_at(data, v1_header_size + 2);
    const u8 hardware = data[v1_header_size + 4];

    // hardware mode is numbered differently in the two versions. in
    // both, the low numbers are the 48K family and anything above is a
    // 128K or a clone with memory this machine does not have.
    const u8 highest_48k_mode = version == 2 ? 1 : 3;
    if (hardware > highest_48k_mode) {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer,
                      "this is a version %d .z80 for hardware mode %u, "
                      "which is not a 48K machine; only 48K snapshots "
                      "can be restored",
                      version, static_cast<unsigned>(hardware));
        return failure(buffer);
    }

    cpu_registers regs = header_regs;
    regs.pc = pc;

    int pages_loaded = 0;
    std::size_t offset = body_start;

    while (offset + 3 <= data.size()) {
        const std::size_t block_length = word_at(data, offset);
        const int page = data[offset + 2];
        offset += 3;

        // 0xFFFF means the page was stored uncompressed.
        const bool uncompressed = block_length == 0xffff;
        const std::size_t stored =
            uncompressed ? page_size : block_length;

        if (offset + stored > data.size())
            return failure(".z80 image ends inside a memory page");

        const std::span<const u8> block = data.subspan(offset, stored);
        offset += stored;

        if (!page_belongs_to_48k(page))
            continue; // pages a 48K machine has no room for

        std::vector<u8> contents;
        if (uncompressed)
            contents.assign(block.begin(), block.end());
        else
            contents = decompress_z80_block(block, false);

        contents.resize(page_size, 0);
        target.mem().write_block(page_address(page), contents, false);
        ++pages_loaded;
    }

    if (pages_loaded == 0)
        return failure(".z80 image contains no pages for a 48K machine");

    target.processor().set_registers(regs);

    load_result result;
    result.ok = true;
    result.bytes = data.size();
    result.entry = regs.pc;

    char buffer[160];
    std::snprintf(buffer, sizeof buffer,
                  "restored a version %d .z80 snapshot, %d pages, "
                  "resuming at 0x%04X",
                  version, pages_loaded, regs.pc);
    result.description = buffer;
    return result;
}

} // namespace

load_result load_z80_snapshot(machine &target, std::span<const u8> data)
{
    if (data.size() < v1_header_size)
        return failure(".z80 image is shorter than its 30 byte header");

    target.reset(true);

    const cpu_registers header_regs = read_common_header(data);
    target.video().set_border(border_from_header(data));

    // a zero program counter in the old header is the flag that says a
    // newer header follows; a real program never starts at 0x0000.
    const u16 v1_pc = word_at(data, 6);

    if (v1_pc != 0)
        return load_v1(target, data, header_regs);

    return load_v2_or_v3(target, data, header_regs);
}

} // namespace spectrum
