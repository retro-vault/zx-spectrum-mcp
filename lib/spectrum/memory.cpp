//
// implementation of the 48K address space.
//
// also holds the built-in stub ROM. the stub is deliberately tiny: it
// only has to leave the machine in a state where frames advance and
// interrupts fire, so that the server is useful for running loaded
// machine code even when no real ROM image is available.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/memory.h"

#include <algorithm>

namespace spectrum {

namespace {

//
// minimal replacement for the Sinclair ROM.
//
// enough to bring the CPU up in a defined state and keep it there:
// disable interrupts, put the stack just below the display file, select
// interrupt mode 1, enable interrupts, then halt in a loop. the mode 1
// handler at 0x0038 does nothing but re-enable and return, so the frame
// interrupt still ticks and any loaded program sees a running machine.
//
struct stub_byte {
    u16 address;
    u8 value;
};

constexpr stub_byte stub_rom[] = {
    // reset vector at 0x0000
    {0x0000, 0xf3}, // di
    {0x0001, 0x31}, // ld sp,0x5b00
    {0x0002, 0x00},
    {0x0003, 0x5b},
    {0x0004, 0xed}, // im 1
    {0x0005, 0x56},
    {0x0006, 0xfb}, // ei
    {0x0007, 0x76}, // halt
    {0x0008, 0x18}, // jr -3, back to the halt
    {0x0009, 0xfd},

    // maskable interrupt handler at 0x0038
    {0x0038, 0xfb}, // ei
    {0x0039, 0xc9}, // ret

    // non maskable handler at 0x0066
    {0x0066, 0xed}, // retn
    {0x0067, 0x45},
};

} // namespace

memory::memory() { reset(); }

void memory::install_stub_rom()
{
    rom_.fill(0xff);
    for (const stub_byte &b : stub_rom)
        rom_[b.address] = b.value;
    custom_rom_ = false;
}

void memory::reset(u8 fill)
{
    ram_.fill(fill);
    if (!custom_rom_)
        install_stub_rom();
}

u8 memory::read(u16 address) const
{
    if (address < rom_size)
        return rom_[address];
    return ram_[address - rom_size];
}

void memory::write(u16 address, u8 value)
{
    // the ROM is not writable from the CPU side.
    if (address < rom_size)
        return;
    ram_[address - rom_size] = value;
}

u8 memory::peek(u16 address) const { return read(address); }

void memory::poke(u16 address, u8 value)
{
    if (address < rom_size)
        rom_[address] = value;
    else
        ram_[address - rom_size] = value;
}

std::vector<u8> memory::read_block(u16 address, std::size_t length) const
{
    std::vector<u8> out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        const auto a = static_cast<u16>((address + i) & 0xffff);
        out.push_back(read(a));
    }
    return out;
}

std::size_t memory::write_block(u16 address, std::span<const u8> data,
                                bool allow_rom)
{
    std::size_t written = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto a = static_cast<u16>((address + i) & 0xffff);
        if (a < rom_size && !allow_rom)
            continue;
        poke(a, data[i]);
        ++written;
    }
    return written;
}

void memory::load_rom(std::span<const u8> data)
{
    rom_.fill(0xff);
    const std::size_t count = std::min(data.size(), rom_size);
    std::copy_n(data.begin(), count, rom_.begin());
    custom_rom_ = true;
}

bool memory::has_custom_rom() const { return custom_rom_; }

std::span<const u8> memory::screen() const
{
    return std::span<const u8>(ram_.data() +
                                   (screen_base - rom_size),
                               screen_bytes);
}

} // namespace spectrum
