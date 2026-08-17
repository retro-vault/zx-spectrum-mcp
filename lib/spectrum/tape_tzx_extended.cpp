//
// TZX sampled-waveform and generalized-data block decoding.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "tape_tzx_internal.h"

#include <limits>
#include <span>
#include <vector>

namespace spectrum::tape_internal {

namespace {

bool extended_error(const tzx_block &block, const std::string &message,
                    std::string &error)
{
    error = "TZX block at byte " + std::to_string(block.file_offset) +
            ": " + message;
    return false;
}

bool append_csw_rle(builder &out, std::span<const u8> encoded,
                    u32 pulse_count, u32 sample_rate,
                    const tzx_block &block, std::string &error)
{
    std::size_t offset = 0;
    for (u32 pulse = 0; pulse < pulse_count; ++pulse) {
        if (offset >= encoded.size())
            return extended_error(block, "CSW pulse data is truncated",
                                  error);

        u64 samples = encoded[offset++];
        if (samples == 0) {
            if (encoded.size() - offset < 4)
                return extended_error(block,
                                      "CSW extended pulse is truncated",
                                      error);
            samples = tzx_u32(encoded, offset);
            offset += 4;
        }
        if (samples == 0)
            return extended_error(block, "CSW pulse length is zero", error);

        if (samples > (std::numeric_limits<u64>::max() - sample_rate / 2) /
                          3'500'000)
            return extended_error(block, "CSW pulse duration overflows",
                                  error);
        const u64 tstates =
            (samples * 3'500'000 + sample_rate / 2) / sample_rate;
        if (!out.append_pulse(tstates == 0 ? 1 : tstates)) {
            error = out.error();
            return false;
        }
    }
    if (offset != encoded.size())
        return extended_error(block, "CSW data has trailing bytes", error);
    return true;
}

struct generalized_symbol {
    u8 flags = 0;
    std::vector<u16> pulses;
};

bool read_symbols(std::span<const u8> body, std::size_t &offset,
                  std::size_t count, u8 max_pulses,
                  std::vector<generalized_symbol> &symbols,
                  const tzx_block &block, std::string &error)
{
    const std::size_t row = 1 + static_cast<std::size_t>(max_pulses) * 2;
    if (count != 0 && row > (body.size() - offset) / count)
        return extended_error(block, "generalized symbol table is truncated",
                              error);

    symbols.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        generalized_symbol symbol;
        symbol.flags = body[offset++];
        symbol.pulses.reserve(max_pulses);
        for (std::size_t pulse = 0; pulse < max_pulses; ++pulse) {
            symbol.pulses.push_back(tzx_u16(body, offset));
            offset += 2;
        }
        symbols.push_back(std::move(symbol));
    }
    return true;
}

bool append_symbol(builder &out, const generalized_symbol &symbol,
                   const tzx_block &block, std::string &error)
{
    switch (symbol.flags & 0x03) {
    case 0:
        // The builder already points at the level opposite the last pulse.
        break;
    case 1:
        // Begin without an edge, at the previous pulse's level.
        out.set_level(!out.level());
        break;
    case 2: out.set_level(false); break;
    case 3: out.set_level(true); break;
    }

    for (u16 duration : symbol.pulses) {
        if (duration == 0)
            break;
        if (!out.append_pulse(duration)) {
            error = out.error();
            return false;
        }
    }
    if (symbol.pulses.empty() || symbol.pulses.front() == 0)
        return extended_error(block,
                              "generalized symbol has no pulse data", error);
    return true;
}

unsigned symbol_bits(std::size_t alphabet_size)
{
    unsigned bits = 0;
    for (std::size_t value = alphabet_size - 1; value != 0; value >>= 1)
        ++bits;
    return bits;
}

bool read_packed_symbol(std::span<const u8> data, u64 bit_offset,
                        unsigned bits, u16 &symbol)
{
    symbol = 0;
    for (unsigned bit = 0; bit < bits; ++bit) {
        const u64 at = bit_offset + bit;
        if (at / 8 >= data.size())
            return false;
        symbol = static_cast<u16>((symbol << 1) |
                                  ((data[at / 8] >> (7 - at % 8)) & 1));
    }
    return true;
}

} // namespace

bool append_tzx_csw(builder &out, const tzx_block &block,
                    std::string &error)
{
    const auto body = std::span<const u8>(block.body);
    if (body.size() < 14)
        return extended_error(block, "CSW header is truncated", error);

    const u32 sample_rate = tzx_u24(body, 6);
    const u8 compression = body[9];
    const u32 pulse_count = tzx_u32(body, 10);
    if (sample_rate == 0)
        return extended_error(block, "CSW sample rate is zero", error);
    if (pulse_count > builder::max_events)
        return extended_error(block, "CSW pulse count exceeds the limit",
                              error);

    const auto source = body.subspan(14);
    std::vector<u8> inflated;
    std::span<const u8> rle = source;
    if (compression == 2) {
        const std::size_t capacity = static_cast<std::size_t>(pulse_count) * 5;
        inflated.resize(capacity == 0 ? 1 : capacity);
        mz_ulong size = static_cast<mz_ulong>(inflated.size());
        const int status = mz_uncompress(
            inflated.data(), &size, source.data(),
            static_cast<mz_ulong>(source.size()));
        if (status != MZ_OK)
            return extended_error(block, "invalid Z-RLE CSW stream", error);
        if (size > capacity)
            return extended_error(block, "CSW pulse stream is too large",
                                  error);
        inflated.resize(static_cast<std::size_t>(size));
        rle = inflated;
    } else if (compression != 1) {
        return extended_error(block, "unknown CSW compression type", error);
    }

    if (!append_csw_rle(out, rle, pulse_count, sample_rate, block, error))
        return false;
    if (!out.append_pause(tzx_u16(body, 4))) {
        error = out.error();
        return false;
    }
    return true;
}

bool append_tzx_generalized(builder &out, const tzx_block &block,
                            std::string &error)
{
    const auto body = std::span<const u8>(block.body);
    if (body.size() < 18)
        return extended_error(block,
                              "generalized-data header is truncated", error);

    const u32 pilot_total = tzx_u32(body, 6);
    const u8 pilot_max_pulses = body[10];
    const std::size_t pilot_alphabet = body[11] == 0 ? 256 : body[11];
    const u32 data_total = tzx_u32(body, 12);
    const u8 data_max_pulses = body[16];
    const std::size_t data_alphabet = body[17] == 0 ? 256 : body[17];
    std::size_t offset = 18;

    if (pilot_total != 0) {
        if (pilot_max_pulses == 0)
            return extended_error(block,
                                  "pilot symbols have zero maximum pulses",
                                  error);
        std::vector<generalized_symbol> symbols;
        if (!read_symbols(body, offset, pilot_alphabet, pilot_max_pulses,
                          symbols, block, error))
            return false;

        u64 expanded = 0;
        for (u32 record = 0; record < pilot_total; ++record) {
            if (body.size() - offset < 3)
                return extended_error(block,
                                      "pilot symbol stream is truncated",
                                      error);
            const u8 index = body[offset++];
            const u16 repeats = tzx_u16(body, offset);
            offset += 2;
            if (index >= symbols.size() || repeats == 0)
                return extended_error(block,
                                      "invalid pilot symbol record", error);
            expanded += repeats;
            if (expanded > builder::max_events)
                return extended_error(
                    block, "generalized pilot expands beyond the limit",
                    error);
            for (u16 i = 0; i < repeats; ++i) {
                if (!append_symbol(out, symbols[index], block, error))
                    return false;
            }
        }
    }

    if (data_total != 0) {
        if (data_total > builder::max_events)
            return extended_error(
                block, "generalized data expands beyond the limit", error);
        if (data_max_pulses == 0)
            return extended_error(block,
                                  "data symbols have zero maximum pulses",
                                  error);
        std::vector<generalized_symbol> symbols;
        if (!read_symbols(body, offset, data_alphabet, data_max_pulses,
                          symbols, block, error))
            return false;

        const unsigned bits = symbol_bits(data_alphabet);
        if (bits != 0 && data_total >
                             std::numeric_limits<u64>::max() / bits)
            return extended_error(block,
                                  "generalized data bit count overflows",
                                  error);
        const u64 needed_bits = static_cast<u64>(data_total) * bits;
        const u64 needed_bytes = (needed_bits + 7) / 8;
        if (needed_bytes > body.size() - offset)
            return extended_error(block,
                                  "generalized data stream is truncated",
                                  error);

        const auto packed = body.subspan(
            offset, static_cast<std::size_t>(needed_bytes));
        for (u32 i = 0; i < data_total; ++i) {
            u16 index = 0;
            if (!read_packed_symbol(packed, static_cast<u64>(i) * bits,
                                    bits, index) ||
                index >= symbols.size())
                return extended_error(block,
                                      "invalid generalized data symbol",
                                      error);
            if (!append_symbol(out, symbols[index], block, error))
                return false;
        }
        offset += static_cast<std::size_t>(needed_bytes);
    }

    if (offset != body.size())
        return extended_error(block,
                              "generalized-data block has trailing bytes",
                              error);
    if (!out.append_pause(tzx_u16(body, 4))) {
        error = out.error();
        return false;
    }
    return true;
}

} // namespace spectrum::tape_internal
