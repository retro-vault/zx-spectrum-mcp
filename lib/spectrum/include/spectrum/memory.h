//
// the 48K address space: 16K of ROM followed by 48K of RAM.
//
// two views of memory are offered on purpose. read() and write() are
// what the CPU sees, so a write into the ROM area is silently dropped
// exactly as it is on real hardware. peek() and poke() are what the
// debugger sees, and poke() will happily overwrite ROM because a
// debugging tool that cannot patch a ROM routine is not much of a
// debugging tool.
//
// no ROM image ships with this project. the Spectrum ROM is still under
// copyright, so the class boots with a small built-in stub that brings
// the machine up in interrupt mode 1 and idles. load a real ROM to get
// BASIC.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_MEMORY_H
#define SPECTRUM_MEMORY_H

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "spectrum/types.h"

namespace spectrum {

//
// flat 64K address space of a 48K machine.
//
class memory {
public:
    static constexpr std::size_t rom_size = 0x4000;
    static constexpr std::size_t ram_size = 0xc000;
    static constexpr std::size_t address_space = 0x10000;

    // start of the display file and of its attribute block.
    static constexpr u16 screen_base = 0x4000;
    static constexpr u16 attribute_base = 0x5800;

    static constexpr std::size_t bitmap_bytes = 6144;
    static constexpr std::size_t attribute_bytes = 768;
    static constexpr std::size_t screen_bytes = 6912;

    memory();

    //
    // Clear RAM and reload the currently selected ROM image.
    //
    // Parameters:
    //      fill        - byte written across the whole of RAM.
    //
    void reset(u8 fill = 0);

    //
    // CPU read. always succeeds; unmapped reads cannot happen because
    // the whole 64K is backed.
    //
    u8 read(u16 address) const;

    //
    // CPU write. addresses below 0x4000 are ignored.
    //
    void write(u16 address, u8 value);

    //
    // Debugger read. identical to read() today, kept separate so that
    // future banked models can expose a physical rather than a logical
    // view without disturbing the CPU path.
    //
    u8 peek(u16 address) const;

    //
    // Debugger write. unlike write() this may modify the ROM area.
    //
    void poke(u16 address, u8 value);

    //
    // Read a run of bytes, wrapping at the top of the address space.
    //
    // Parameters:
    //      address     - first address to read.
    //      length      - how many bytes to return.
    //
    // Returns:
    //      the bytes read, in address order.
    //
    std::vector<u8> read_block(u16 address, std::size_t length) const;

    //
    // Write a run of bytes, wrapping at the top of the address space.
    //
    // Parameters:
    //      address     - first address to write.
    //      data        - bytes to store.
    //      allow_rom   - when false, writes below 0x4000 are skipped.
    //
    // Returns:
    //      how many bytes were actually stored.
    //
    std::size_t write_block(u16 address, std::span<const u8> data,
                            bool allow_rom);

    //
    // Replace the ROM image.
    //
    // Parameters:
    //      data        - up to 16K. a short image is zero padded, a
    //                    long one is truncated.
    //
    void load_rom(std::span<const u8> data);

    //
    // Returns:
    //      true once a ROM image other than the built-in stub is in
    //      place.
    //
    bool has_custom_rom() const;

    //
    // Returns:
    //      a read only view of the 6912 byte display file.
    //
    std::span<const u8> screen() const;

private:
    std::array<u8, rom_size> rom_{};
    std::array<u8, ram_size> ram_{};
    bool custom_rom_ = false;

    void install_stub_rom();
};

} // namespace spectrum

#endif // SPECTRUM_MEMORY_H
