//
// TZX container parsing, control flow, and pulse-block decoding.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "tape_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

#include "tape_tzx_internal.h"

namespace spectrum::tape_internal {

namespace {

constexpr std::array<u8, 8> signature = {
    'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1a,
};
constexpr std::size_t max_block_executions = 1'000'000;

std::string block_error(const tzx_block &block, const std::string &message)
{
    char id[8];
    std::snprintf(id, sizeof id, "%02X", block.id);
    return "TZX block 0x" + std::string(id) + " at byte " +
           std::to_string(block.file_offset) + ": " + message;
}

bool take_block(std::span<const u8> data, std::size_t &offset,
                tzx_block &block, std::string &error)
{
    block.file_offset = offset;
    block.id = data[offset++];
    const std::size_t available = data.size() - offset;
    std::size_t length = 0;

    const auto need = [&](std::size_t count) {
        if (available >= count)
            return true;
        error = block_error(block, "truncated block header");
        return false;
    };
    const auto sized8 = [&](std::size_t prefix, std::size_t at,
                            std::size_t scale = 1) {
        if (!need(prefix))
            return false;
        const std::size_t count = data[offset + at];
        if (count > (std::numeric_limits<std::size_t>::max() - prefix) /
                        scale)
            return false;
        length = prefix + count * scale;
        return true;
    };
    const auto sized16 = [&](std::size_t prefix, std::size_t at,
                             std::size_t scale = 1) {
        if (!need(prefix))
            return false;
        const std::size_t count = tzx_u16(data.subspan(offset), at);
        length = prefix + count * scale;
        return true;
    };
    const auto sized24 = [&](std::size_t prefix, std::size_t at) {
        if (!need(prefix))
            return false;
        length = prefix + tzx_u24(data.subspan(offset), at);
        return true;
    };
    const auto sized32 = [&](std::size_t prefix, std::size_t at) {
        if (!need(prefix))
            return false;
        length = prefix + static_cast<std::size_t>(
                              tzx_u32(data.subspan(offset), at));
        return true;
    };

    bool known = true;
    switch (block.id) {
    case 0x10: known = sized16(4, 2); break;
    case 0x11: known = sized24(18, 15); break;
    case 0x12: length = 4; break;
    case 0x13: known = sized8(1, 0, 2); break;
    case 0x14: known = sized24(10, 7); break;
    case 0x15: known = sized24(8, 5); break;
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19: known = sized32(4, 0); break;
    case 0x20: length = 2; break;
    case 0x21: known = sized8(1, 0); break;
    case 0x22: length = 0; break;
    case 0x23:
    case 0x24: length = 2; break;
    case 0x25:
    case 0x27: length = 0; break;
    case 0x26: known = sized16(2, 0, 2); break;
    case 0x28: known = sized16(2, 0); break;
    case 0x2a:
    case 0x2b: known = sized32(4, 0); break;
    case 0x30: known = sized8(1, 0); break;
    case 0x31: known = sized8(2, 1); break;
    case 0x32: known = sized16(2, 0); break;
    case 0x33: known = sized8(1, 0, 3); break;
    case 0x34: length = 8; break;
    case 0x35: known = sized32(20, 16); break;
    case 0x40: known = sized24(4, 1); break;
    case 0x5a: length = 9; break;
    default:
        known = false;
        error = block_error(block, "unknown block type");
        break;
    }

    if (!known)
        return false;
    if (length > available) {
        error = block_error(block, "block data is truncated");
        return false;
    }
    block.body.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                      data.begin() +
                          static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return true;
}

bool parse_blocks(std::span<const u8> data, std::vector<tzx_block> &blocks,
                  std::string &error)
{
    if (data.size() < 10 || !std::equal(signature.begin(), signature.end(),
                                         data.begin())) {
        error = "not a TZX image (missing ZXTape signature)";
        return false;
    }
    if (data[8] > 1) {
        error = "unsupported TZX major version " + std::to_string(data[8]);
        return false;
    }

    std::size_t offset = 10;
    while (offset < data.size()) {
        tzx_block block;
        if (!take_block(data, offset, block, error))
            return false;
        blocks.push_back(std::move(block));
    }
    return true;
}

bool append_standard(builder &out, std::span<const u8> body)
{
    const auto bytes = body.subspan(4, tzx_u16(body, 2));
    const u16 pilot_count = !bytes.empty() && bytes.front() >= 0x80
                                ? 3223
                                : 8063;
    return out.append_pulses(2168, pilot_count) &&
           out.append_pulse(667) && out.append_pulse(735) &&
           out.append_data(bytes, 855, 1710, 8) &&
           out.append_pause(tzx_u16(body, 0));
}

bool append_turbo(builder &out, std::span<const u8> body)
{
    return out.append_pulses(tzx_u16(body, 0), tzx_u16(body, 10)) &&
           out.append_pulse(tzx_u16(body, 2)) &&
           out.append_pulse(tzx_u16(body, 4)) &&
           out.append_data(body.subspan(18, tzx_u24(body, 15)),
                           tzx_u16(body, 6), tzx_u16(body, 8), body[12]) &&
           out.append_pause(tzx_u16(body, 13));
}

bool append_pure_data(builder &out, std::span<const u8> body)
{
    return out.append_data(body.subspan(10, tzx_u24(body, 7)),
                           tzx_u16(body, 0), tzx_u16(body, 2), body[4]) &&
           out.append_pause(tzx_u16(body, 5));
}

bool append_direct(builder &out, std::span<const u8> body)
{
    const u16 sample_length = tzx_u16(body, 0);
    const u8 last_bits = body[4];
    const auto samples = body.subspan(8, tzx_u24(body, 5));
    if (sample_length == 0)
        return false;
    if (!samples.empty() && (last_bits == 0 || last_bits > 8))
        return false;
    const u64 sample_count = samples.empty()
                                 ? 0
                                 : static_cast<u64>(samples.size() - 1) * 8 +
                                       last_bits;
    if (sample_count > builder::max_events)
        return false;

    bool final_level = out.level();
    for (std::size_t byte = 0; byte < samples.size(); ++byte) {
        const int bits = byte + 1 == samples.size() ? last_bits : 8;
        for (int bit = 0; bit < bits; ++bit) {
            const bool level = (samples[byte] & (0x80 >> bit)) != 0;
            final_level = level;
            out.set_level(level);
            if (!out.append_span(level, sample_length))
                return false;
        }
    }
    // Pulse-oriented blocks which follow begin with an edge. Keep the
    // builder pointing at the level opposite the last absolute sample.
    if (!samples.empty())
        out.set_level(!final_level);
    return out.append_pause(tzx_u16(body, 2));
}

std::optional<std::size_t> relative_target(std::size_t pc, i16 offset,
                                           std::size_t count)
{
    const std::int64_t target = static_cast<std::int64_t>(pc) + offset;
    if (target < 0 || target >= static_cast<std::int64_t>(count))
        return std::nullopt;
    return static_cast<std::size_t>(target);
}

void extract_archive_title(const tzx_block &block, std::string &title)
{
    if (!title.empty() || block.id != 0x32 || block.body.size() < 3)
        return;
    std::size_t offset = 3;
    const unsigned count = block.body[2];
    for (unsigned i = 0; i < count && offset + 2 <= block.body.size(); ++i) {
        const u8 id = block.body[offset++];
        const std::size_t length = block.body[offset++];
        if (length > block.body.size() - offset)
            return;
        if (id == 0) {
            for (std::size_t j = 0; j < length; ++j) {
                const u8 c = block.body[offset + j];
                if (c < 0x80) {
                    title.push_back(static_cast<char>(c));
                } else {
                    title.push_back(static_cast<char>(0xc0 | (c >> 6)));
                    title.push_back(static_cast<char>(0x80 | (c & 0x3f)));
                }
            }
            return;
        }
        offset += length;
    }
}

bool first_selection(const tzx_block &block, i16 &choice,
                     std::string &error)
{
    const auto body = std::span<const u8>(block.body);
    if (body.size() < 3 || body[2] == 0) {
        error = block_error(block, "selection list is empty");
        return false;
    }

    std::size_t offset = 3;
    for (u8 i = 0; i < body[2]; ++i) {
        if (body.size() - offset < 3) {
            error = block_error(block, "selection list is truncated");
            return false;
        }
        const i16 target = static_cast<i16>(tzx_u16(body, offset));
        const std::size_t text_length = body[offset + 2];
        offset += 3;
        if (text_length > body.size() - offset) {
            error = block_error(block, "selection text is truncated");
            return false;
        }
        if (i == 0)
            choice = target;
        offset += text_length;
    }
    if (offset != body.size()) {
        error = block_error(block, "selection list has trailing bytes");
        return false;
    }
    return true;
}

} // namespace

bool decode_tzx(std::span<const u8> data, image &result,
                std::string &error)
{
    std::vector<tzx_block> blocks;
    if (!parse_blocks(data, blocks, error))
        return false;

    std::size_t data_blocks = 0;
    std::string title;
    for (const tzx_block &block : blocks) {
        if (block.id == 0x10 || block.id == 0x11 || block.id == 0x14 ||
            block.id == 0x15 || block.id == 0x18 || block.id == 0x19)
            ++data_blocks;
        extract_archive_title(block, title);
    }

    builder out("tzx", data.size());
    struct loop_state { std::size_t start; u16 remaining; };
    std::vector<loop_state> loops;
    struct call_state {
        std::size_t origin;
        std::vector<i16> targets;
        std::size_t next = 0;
    };
    std::vector<call_state> calls;

    std::size_t pc = 0;
    std::size_t executions = 0;
    while (pc < blocks.size()) {
        if (++executions > max_block_executions) {
            error = "TZX control flow exceeds the 1000000 block limit";
            return false;
        }
        const tzx_block &block = blocks[pc];
        const auto body = std::span<const u8>(block.body);
        std::size_t next = pc + 1;
        bool ok = true;

        switch (block.id) {
        case 0x10: ok = append_standard(out, body); break;
        case 0x11: ok = append_turbo(out, body); break;
        case 0x12:
            ok = out.append_pulses(tzx_u16(body, 0), tzx_u16(body, 2));
            break;
        case 0x13:
            for (std::size_t i = 0; ok && i < body[0]; ++i)
                ok = out.append_pulse(tzx_u16(body, 1 + i * 2));
            break;
        case 0x14: ok = append_pure_data(out, body); break;
        case 0x15:
            ok = append_direct(out, body);
            if (!ok && out.good())
                error = block_error(block, "invalid direct recording fields");
            break;
        case 0x16:
        case 0x17:
            error = block_error(block, "deprecated C64 block is unsupported");
            return false;
        case 0x18: ok = append_tzx_csw(out, block, error); break;
        case 0x19: ok = append_tzx_generalized(out, block, error); break;
        case 0x20:
            ok = tzx_u16(body, 0) == 0
                     ? out.append_stop()
                     : out.append_pause(tzx_u16(body, 0));
            break;
        case 0x21:
        case 0x22:
        case 0x30:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x5a:
            break;
        case 0x23: {
            const auto target = relative_target(
                pc, static_cast<i16>(tzx_u16(body, 0)), blocks.size());
            if (!target) {
                error = block_error(block, "jump target is outside the image");
                return false;
            }
            next = *target;
            break;
        }
        case 0x24:
            if (tzx_u16(body, 0) == 0) {
                error = block_error(block, "loop count is zero");
                return false;
            }
            loops.push_back({pc + 1, tzx_u16(body, 0)});
            break;
        case 0x25:
            if (loops.empty()) {
                error = block_error(block, "loop end has no loop start");
                return false;
            }
            if (--loops.back().remaining != 0)
                next = loops.back().start;
            else
                loops.pop_back();
            break;
        case 0x26: {
            call_state call;
            call.origin = pc;
            const std::size_t count = tzx_u16(body, 0);
            for (std::size_t i = 0; i < count; ++i)
                call.targets.push_back(
                    static_cast<i16>(tzx_u16(body, 2 + i * 2)));
            if (call.targets.empty())
                break;
            const auto target = relative_target(pc, call.targets.front(),
                                                blocks.size());
            if (!target) {
                error = block_error(block, "call target is outside the image");
                return false;
            }
            calls.push_back(std::move(call));
            next = *target;
            break;
        }
        case 0x27:
            if (calls.empty()) {
                error = block_error(block, "return has no call sequence");
                return false;
            } else if (++calls.back().next < calls.back().targets.size()) {
                const auto target = relative_target(
                    calls.back().origin,
                    calls.back().targets[calls.back().next], blocks.size());
                if (!target) {
                    error = block_error(block,
                                        "call target is outside the image");
                    return false;
                }
                next = *target;
            } else {
                next = calls.back().origin + 1;
                calls.pop_back();
            }
            break;
        case 0x28: {
            i16 choice;
            if (!first_selection(block, choice, error))
                return false;
            if (const auto target = relative_target(pc, choice,
                                                    blocks.size())) {
                next = *target;
            } else {
                error = block_error(block,
                                    "selection target is outside the image");
                return false;
            }
            break;
        }
        case 0x2a: ok = out.append_stop(); break;
        case 0x2b:
            if (body.size() != 5 || body[4] > 1) {
                error = block_error(block, "invalid signal-level block");
                return false;
            }
            out.set_level(body[4] != 0);
            break;
        case 0x31:
            if (!body.empty() && body[0] == 0)
                ok = out.append_stop();
            break;
        case 0x40:
            error = block_error(block, "embedded snapshot is unsupported");
            return false;
        }

        if (!ok) {
            if (error.empty())
                error = block_error(block, out.error().empty()
                                               ? "invalid block fields"
                                               : out.error());
            return false;
        }
        pc = next;
    }

    if (!loops.empty()) {
        error = "TZX loop start has no loop end";
        return false;
    }
    if (!calls.empty()) {
        error = "TZX call sequence has no return";
        return false;
    }
    result = out.finish(blocks.size(), data_blocks, std::move(title));
    error.clear();
    return true;
}

} // namespace spectrum::tape_internal
