//
// TAP decoding, common waveform construction, and cassette transport.
//
// Standard blocks are expanded to the ROM's pilot, sync and two-pulse bit
// encoding. Playback then walks the compact list of constant-level spans;
// long host calls remain cheap because tick() skips whole spans at once.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/tape.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "tape_internal.h"

namespace spectrum::tape_internal {

namespace {

constexpr u16 pilot_pulse = 2168;
constexpr u16 sync_pulse_1 = 667;
constexpr u16 sync_pulse_2 = 735;
constexpr u16 zero_pulse = 855;
constexpr u16 one_pulse = 1710;
constexpr u16 header_pulses = 8063;
constexpr u16 data_pulses = 3223;
constexpr u64 tstates_per_millisecond = 3500;

u16 read_u16(std::span<const u8> data, std::size_t offset)
{
    return static_cast<u16>(data[offset] | (data[offset + 1] << 8));
}

bool append_standard_block(builder &out, std::span<const u8> data,
                           u16 pause)
{
    const u16 count = !data.empty() && data.front() >= 0x80
                          ? data_pulses
                          : header_pulses;
    return out.append_pulses(pilot_pulse, count) &&
           out.append_pulse(sync_pulse_1) &&
           out.append_pulse(sync_pulse_2) &&
           out.append_data(data, zero_pulse, one_pulse, 8) &&
           out.append_pause(pause);
}

} // namespace

builder::builder(std::string format, std::size_t image_bytes)
{
    image_.format = std::move(format);
    image_.image_bytes = image_bytes;
}

bool builder::fail(std::string message)
{
    if (error_.empty())
        error_ = std::move(message);
    return false;
}

bool builder::append_span(bool level, u64 duration)
{
    if (!good())
        return false;
    if (duration == 0)
        return true;
    if (image_.duration_tstates >
        std::numeric_limits<u64>::max() - duration) {
        return fail("tape duration overflows its T-state counter");
    }

    if (!image_.events.empty()) {
        event &last = image_.events.back();
        if (last.kind == event_kind::span && last.level == level &&
            last.duration <= std::numeric_limits<u64>::max() - duration) {
            last.duration += duration;
            image_.duration_tstates += duration;
            return true;
        }
    }

    if (image_.events.size() >= max_events)
        return fail("tape expands beyond the 4000000 segment limit");

    image_.events.push_back({event_kind::span, duration, level});
    image_.duration_tstates += duration;
    return true;
}

bool builder::append_pulse(u64 duration)
{
    if (duration == 0)
        return fail("a tape pulse has zero duration");
    if (!append_span(level_, duration))
        return false;
    level_ = !level_;
    return true;
}

bool builder::append_pulses(u64 duration, u64 count)
{
    for (u64 i = 0; i < count; ++i) {
        if (!append_pulse(duration))
            return false;
    }
    return true;
}

bool builder::append_data(std::span<const u8> data, u16 zero_length,
                          u16 one_length, u8 used_bits_last)
{
    if (data.empty())
        return true;
    if (zero_length == 0 || one_length == 0)
        return fail("a tape data pulse has zero duration");
    if (used_bits_last == 0 || used_bits_last > 8)
        return fail("used bits in the last tape byte must be 1 to 8");

    for (std::size_t byte = 0; byte < data.size(); ++byte) {
        const int bits = byte + 1 == data.size() ? used_bits_last : 8;
        for (int bit = 0; bit < bits; ++bit) {
            const bool one = (data[byte] & (0x80 >> bit)) != 0;
            const u16 duration = one ? one_length : zero_length;
            if (!append_pulse(duration) || !append_pulse(duration))
                return false;
        }
    }
    return true;
}

bool builder::append_pause(u16 milliseconds)
{
    if (milliseconds == 0)
        return true;

    const u64 total =
        static_cast<u64>(milliseconds) * tstates_per_millisecond;
    const u64 finishing = std::min(total, tstates_per_millisecond);

    // Finish the final edge at the opposite level for at least 1 ms, then
    // hold silence low for the rest of the requested pause.
    if (!append_span(level_, finishing))
        return false;
    if (total > finishing && !append_span(false, total - finishing))
        return false;
    level_ = false;
    return true;
}

bool builder::append_stop()
{
    if (!good())
        return false;
    if (image_.events.size() >= max_events)
        return fail("tape expands beyond the 4000000 segment limit");
    image_.events.push_back({event_kind::stop, 0, false});
    return true;
}

void builder::set_level(bool level) { level_ = level; }

bool builder::good() const { return error_.empty(); }

const std::string &builder::error() const { return error_; }

bool builder::level() const { return level_; }

image builder::finish(std::size_t blocks, std::size_t data_blocks,
                      std::string title)
{
    image_.blocks = blocks;
    image_.data_blocks = data_blocks;
    image_.title = std::move(title);
    return std::move(image_);
}

bool decode_tap(std::span<const u8> data, image &result,
                std::string &error)
{
    builder out("tap", data.size());
    std::size_t offset = 0;
    std::size_t blocks = 0;

    while (offset < data.size()) {
        if (data.size() - offset < 2) {
            error = "truncated TAP block length at byte " +
                    std::to_string(offset);
            return false;
        }

        const std::size_t length = read_u16(data, offset);
        offset += 2;
        if (length > data.size() - offset) {
            error = "TAP block " + std::to_string(blocks) +
                    " is truncated";
            return false;
        }

        if (!append_standard_block(out, data.subspan(offset, length), 1000)) {
            error = out.error();
            return false;
        }
        offset += length;
        ++blocks;
    }

    result = out.finish(blocks, blocks);
    error.clear();
    return true;
}

} // namespace spectrum::tape_internal

namespace spectrum {

class tape_deck::impl {
public:
    bool insert_tap(std::span<const u8> data, bool autoplay,
                    std::string &error)
    {
        tape_internal::image decoded;
        if (!tape_internal::decode_tap(data, decoded, error))
            return false;
        install(std::move(decoded), autoplay);
        return true;
    }

    bool insert_tzx(std::span<const u8> data, bool autoplay,
                    std::string &error)
    {
        tape_internal::image decoded;
        if (!tape_internal::decode_tzx(data, decoded, error))
            return false;
        install(std::move(decoded), autoplay);
        return true;
    }

    bool play()
    {
        if (!loaded_ || finished_)
            return false;

        if (stopped_by_command_) {
            stopped_by_command_ = false;
            playing_ = true;
            prepare_event();
            return playing_;
        }

        playing_ = true;
        if (event_index_ < image_.events.size() &&
            image_.events[event_index_].kind ==
                tape_internal::event_kind::stop) {
            consume_stop();
        }
        return playing_;
    }

    void stop() { playing_ = false; }

    void rewind()
    {
        event_index_ = 0;
        event_remaining_ = 0;
        position_tstates_ = 0;
        playing_ = false;
        finished_ = image_.events.empty();
        stopped_by_command_ = false;
        level_ = false;

        if (!finished_ && image_.events.front().kind ==
                              tape_internal::event_kind::span) {
            event_remaining_ = image_.events.front().duration;
            level_ = image_.events.front().level;
        }
    }

    void eject()
    {
        image_ = {};
        loaded_ = false;
        rewind();
        finished_ = false;
    }

    void tick(u64 tstates)
    {
        while (playing_ && tstates > 0) {
            if (event_index_ >= image_.events.size()) {
                finish();
                break;
            }

            if (image_.events[event_index_].kind ==
                tape_internal::event_kind::stop) {
                consume_stop();
                break;
            }

            const u64 amount = std::min(tstates, event_remaining_);
            event_remaining_ -= amount;
            position_tstates_ += amount;
            tstates -= amount;

            if (event_remaining_ == 0) {
                ++event_index_;
                prepare_event();
            }
        }
    }

    bool ear_level() const { return loaded_ && playing_ ? level_ : false; }

    tape_status status() const
    {
        tape_status result;
        result.loaded = loaded_;
        result.playing = playing_;
        result.finished = finished_;
        result.stopped_by_command = stopped_by_command_;
        result.ear_level = ear_level();
        result.format = image_.format;
        result.title = image_.title;
        result.image_bytes = image_.image_bytes;
        result.blocks = image_.blocks;
        result.data_blocks = image_.data_blocks;
        result.segments = image_.events.size();
        result.segment = event_index_;
        result.duration_tstates = image_.duration_tstates;
        result.position_tstates = position_tstates_;
        return result;
    }

private:
    void install(tape_internal::image image, bool autoplay)
    {
        image_ = std::move(image);
        loaded_ = true;
        rewind();
        if (autoplay)
            (void)play();
    }

    void prepare_event()
    {
        if (event_index_ >= image_.events.size()) {
            finish();
            return;
        }

        const tape_internal::event &next = image_.events[event_index_];
        if (next.kind == tape_internal::event_kind::stop) {
            consume_stop();
            return;
        }

        event_remaining_ = next.duration;
        level_ = next.level;
    }

    void consume_stop()
    {
        ++event_index_;
        event_remaining_ = 0;
        playing_ = false;
        stopped_by_command_ = true;
        level_ = false;
    }

    void finish()
    {
        event_index_ = image_.events.size();
        event_remaining_ = 0;
        playing_ = false;
        finished_ = true;
        stopped_by_command_ = false;
        level_ = false;
    }

    tape_internal::image image_;
    std::size_t event_index_ = 0;
    u64 event_remaining_ = 0;
    u64 position_tstates_ = 0;
    bool loaded_ = false;
    bool playing_ = false;
    bool finished_ = false;
    bool stopped_by_command_ = false;
    bool level_ = false;
};

tape_deck::tape_deck() : impl_(std::make_unique<impl>()) {}

tape_deck::~tape_deck() = default;

bool tape_deck::insert_tap(std::span<const u8> data, bool autoplay,
                           std::string &error)
{
    return impl_->insert_tap(data, autoplay, error);
}

bool tape_deck::insert_tzx(std::span<const u8> data, bool autoplay,
                           std::string &error)
{
    return impl_->insert_tzx(data, autoplay, error);
}

bool tape_deck::play() { return impl_->play(); }

void tape_deck::stop() { impl_->stop(); }

void tape_deck::rewind() { impl_->rewind(); }

void tape_deck::eject() { impl_->eject(); }

void tape_deck::reset() { impl_->rewind(); }

void tape_deck::tick(u64 tstates) { impl_->tick(tstates); }

bool tape_deck::ear_level() const { return impl_->ear_level(); }

tape_status tape_deck::status() const { return impl_->status(); }

} // namespace spectrum
