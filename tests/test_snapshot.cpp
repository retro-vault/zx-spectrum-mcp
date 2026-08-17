//
// the snapshot loaders.
//
// each format is built by hand here rather than read from a file, so
// the test states exactly what the bytes mean and does not depend on
// any copyrighted image being present.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <vector>

#include "spectrum/snapshot.h"
#include "test_support.h"

using namespace spectrum;

namespace {

void put16(std::vector<u8> &out, std::size_t offset, u16 value)
{
    out[offset] = static_cast<u8>(value & 0xff);
    out[offset + 1] = static_cast<u8>(value >> 8);
}

void test_binary()
{
    test::section("raw binary");

    machine target;
    const std::vector<u8> code = {0x3e, 0x42, 0xc9};

    load_options options;
    options.format = snapshot_format::binary;
    options.address = 0x8000;
    options.start = 0x8000;

    const load_result result = load_into(target, code, options);

    test::check(result.ok, "a binary image loads");
    test::check_eq(static_cast<long long>(result.bytes), 3,
                   "all three bytes were consumed");
    test::check_eq(target.mem().read(0x8000), 0x3e, "the first byte");
    test::check_eq(target.mem().read(0x8002), 0xc9, "the last byte");

    target.run_tstates(1);
    test::check_eq(target.registers().pc, 0x8000,
                   "and execution starts where asked");

    // ROM is protected unless the caller insists.
    load_options into_rom;
    into_rom.format = snapshot_format::binary;
    into_rom.address = 0x0000;
    const load_result blocked = load_into(target, code, into_rom);
    test::check(blocked.ok, "a load into ROM still reports success");
    test::check(target.mem().read(0x0000) != 0x3e,
                "but the bytes did not land in ROM");
}

void test_scr()
{
    test::section(".scr screen dump");

    machine target;
    std::vector<u8> picture(memory::screen_bytes, 0);
    picture[0] = 0xa5;
    picture[memory::bitmap_bytes] = 0x47; // first attribute

    load_options options;
    options.format = snapshot_format::scr;
    const load_result result = load_into(target, picture, options);

    test::check(result.ok, "a .scr loads");
    test::check_eq(target.mem().read(0x4000), 0xa5,
                   "the bitmap lands at 0x4000");
    test::check_eq(target.mem().read(0x5800), 0x47,
                   "and the attributes at 0x5800");

    std::vector<u8> truncated(100, 0);
    test::check(!load_into(target, truncated, options).ok,
                "a short .scr is rejected");
}

void test_sna()
{
    test::section("48K .sna machine state");

    machine target;
    std::vector<u8> image(27 + 49152, 0);

    image[0] = 0x3f;               // I
    put16(image, 1, 0x1111);       // HL'
    put16(image, 3, 0x2222);       // DE'
    put16(image, 5, 0x3333);       // BC'
    put16(image, 7, 0x4444);       // AF'
    put16(image, 9, 0x5555);       // HL
    put16(image, 11, 0x6666);      // DE
    put16(image, 13, 0x7777);      // BC
    put16(image, 15, 0x8888);      // IY
    put16(image, 17, 0x9999);      // IX
    image[19] = 0x04;              // IFF2 set
    image[20] = 0x7f;              // R
    put16(image, 21, 0xaaaa);      // AF
    put16(image, 23, 0xc000);      // SP
    image[25] = 1;                 // interrupt mode 1
    image[26] = 3;                 // magenta border

    // the program counter is recovered by popping it off the stack, so
    // put an address where SP points. 0xC000 is 32768 bytes into RAM.
    const std::size_t stack_offset = 27 + (0xc000 - 0x4000);
    image[stack_offset] = 0x00;
    image[stack_offset + 1] = 0x90; // 0x9000

    load_options options;
    options.format = snapshot_format::sna;
    const load_result result = load_into(target, image, options);

    test::check(result.ok, "a .sna loads");

    const cpu_registers regs = target.processor().registers();
    test::check_eq(regs.i, 0x3f, "I");
    test::check_eq(regs.hl_alt, 0x1111, "HL'");
    test::check_eq(regs.de_alt, 0x2222, "DE'");
    test::check_eq(regs.bc_alt, 0x3333, "BC'");
    test::check_eq(regs.af_alt, 0x4444, "AF'");
    test::check_eq(regs.hl, 0x5555, "HL");
    test::check_eq(regs.de, 0x6666, "DE");
    test::check_eq(regs.bc, 0x7777, "BC");
    test::check_eq(regs.iy, 0x8888, "IY");
    test::check_eq(regs.ix, 0x9999, "IX");
    test::check_eq(regs.af, 0xaaaa, "AF");
    test::check_eq(regs.im, 1, "interrupt mode");
    test::check(regs.iff1 && regs.iff2, "both interrupt flip flops");
    test::check_eq(regs.pc, 0x9000, "pc popped from the stack");
    test::check_eq(regs.sp, 0xc002, "and the stack pointer moved past it");
    test::check_eq(target.video().border(), 3, "the border colour");
    test::check_eq(static_cast<long long>(result.entry), 0x9000,
                   "the entry point is reported");

    std::vector<u8> truncated(100, 0);
    test::check(!load_into(target, truncated, options).ok,
                "a short .sna is rejected");
}

void test_z80_decompression()
{
    test::section(".z80 run length encoding");

    // ED ED count value expands to a run; a lone ED stays literal.
    const std::vector<u8> encoded = {0x01, 0xed, 0xed, 0x04, 0xff,
                                     0x02, 0xed, 0x03};
    const std::vector<u8> plain = decompress_z80_block(encoded, false);

    test::check_eq(static_cast<long long>(plain.size()), 8,
                   "the run expands to the right length");
    test::check_eq(plain[0], 0x01, "the leading literal");
    test::check_eq(plain[1], 0xff, "the run");
    test::check_eq(plain[4], 0xff, "to its end");
    test::check_eq(plain[5], 0x02, "the following literal");
    test::check_eq(plain[6], 0xed, "a lone ED stays literal");

    // version 1 files end at 00 ED ED 00 rather than by length.
    const std::vector<u8> terminated = {0x11, 0x22, 0x00, 0xed,
                                        0xed, 0x00, 0x33};
    const std::vector<u8> stopped = decompress_z80_block(terminated, true);
    test::check_eq(static_cast<long long>(stopped.size()), 2,
                   "the end marker stops decoding");
}

void test_z80_version_1()
{
    test::section(".z80 version 1");

    machine target;
    std::vector<u8> image(30 + 49152, 0);

    image[0] = 0x12;              // A
    image[1] = 0x34;              // F
    put16(image, 2, 0x5678);      // BC
    put16(image, 4, 0x9abc);      // HL
    put16(image, 6, 0x9000);      // PC, non-zero means version 1
    put16(image, 8, 0xc000);      // SP
    image[10] = 0x3f;             // I
    image[11] = 0x40;             // R low bits
    image[12] = 0x04;             // border 2, not compressed
    put16(image, 13, 0xdef0);     // DE
    image[27] = 1;                // IFF1
    image[28] = 1;                // IFF2
    image[29] = 1;                // interrupt mode 1

    image[30] = 0xa5;             // first byte of RAM, at 0x4000

    load_options options;
    options.format = snapshot_format::z80;
    const load_result result = load_into(target, image, options);

    test::check(result.ok, "an uncompressed version 1 .z80 loads");
    test::check_contains(result.description, "version 1",
                         "and says which version it was");

    const cpu_registers regs = target.processor().registers();
    test::check_eq(regs.af, 0x1234, "AF is assembled from two bytes");
    test::check_eq(regs.bc, 0x5678, "BC");
    test::check_eq(regs.hl, 0x9abc, "HL");
    test::check_eq(regs.de, 0xdef0, "DE");
    test::check_eq(regs.pc, 0x9000, "pc comes from the header");
    test::check_eq(regs.sp, 0xc000, "SP");
    test::check_eq(regs.im, 1, "interrupt mode");
    test::check_eq(target.video().border(), 2, "the border colour");
    test::check_eq(target.mem().read(0x4000), 0xa5, "RAM was restored");
}

void test_z80_version_2()
{
    test::section(".z80 version 2");

    machine target;
    std::vector<u8> image(30 + 2 + 23, 0);

    image[0] = 0x01;
    put16(image, 6, 0x0000);      // zero pc flags a newer header
    image[12] = 0x00;
    image[30] = 23;               // extra header length: version 2
    image[31] = 0;
    put16(image, 32, 0xa000);     // the real pc
    image[34] = 0;                // hardware mode 0: 48K

    // one uncompressed page. page 8 is 0x4000 on a 48K.
    std::vector<u8> page(0x4000, 0x5a);
    image.push_back(0xff);        // length 0xFFFF means uncompressed
    image.push_back(0xff);
    image.push_back(8);
    image.insert(image.end(), page.begin(), page.end());

    load_options options;
    options.format = snapshot_format::z80;
    const load_result result = load_into(target, image, options);

    test::check(result.ok, "a version 2 .z80 loads");
    test::check_contains(result.description, "version 2",
                         "and says which version it was");
    test::check_eq(target.processor().registers().pc, 0xa000,
                   "pc comes from the extra header");
    test::check_eq(target.mem().read(0x4000), 0x5a,
                   "page 8 was placed at 0x4000");
    test::check_eq(target.mem().read(0x7fff), 0x5a,
                   "to the end of the page");

    // a 128K snapshot has memory this machine cannot hold.
    image[34] = 4; // hardware mode 4: 128K
    const load_result refused = load_into(target, image, options);
    test::check(!refused.ok, "a 128K snapshot is refused");
    test::check_contains(refused.error, "48K",
                         "and the message says why");
}

void test_format_detection()
{
    test::section("format names and extensions");

    test::check(snapshot_format_from_name("z80") == snapshot_format::z80,
                "by name");
    test::check(snapshot_format_from_name("SNA") == snapshot_format::sna,
                "ignoring case");
    test::check(snapshot_format_from_name("bin") ==
                    snapshot_format::binary,
                "an alias");
    test::check(snapshot_format_from_name("tap") == snapshot_format::tap,
                "TAP by name");
    test::check(snapshot_format_from_name("TZX") == snapshot_format::tzx,
                "TZX ignoring case");

    test::check(snapshot_format_from_path("game.z80") ==
                    snapshot_format::z80,
                "from a path");
    test::check(snapshot_format_from_path("/tmp/A.SCR") ==
                    snapshot_format::scr,
                "from an upper case path");
    test::check(snapshot_format_from_path("loader.tzx") ==
                    snapshot_format::tzx,
                "a tape extension is detected");
    test::check(!snapshot_format_from_path("noextension").has_value(),
                "a path with no extension yields nothing");
}

} // namespace

int main()
{
    test_binary();
    test_scr();
    test_sna();
    test_z80_decompression();
    test_z80_version_1();
    test_z80_version_2();
    test_format_detection();
    return test::summary("snapshot");
}
