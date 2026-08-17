//
// private representation shared by the TAP and TZX decoders.
//
// A level span is intentionally simpler than a simulated audio sample: its
// exact T-state duration is enough to represent ordinary pulses, sampled
// recordings, pauses, and explicit signal levels without rounding again at
// playback time.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_TAPE_INTERNAL_H
#define SPECTRUM_TAPE_INTERNAL_H

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "spectrum/types.h"

namespace spectrum::tape_internal {

enum class event_kind : u8 {
    span,
    stop,
};

struct event {
    event_kind kind = event_kind::span;
    u64 duration = 0;
    bool level = false;
};

struct image {
    std::vector<event> events;
    std::string format;
    std::string title;
    std::size_t image_bytes = 0;
    std::size_t blocks = 0;
    std::size_t data_blocks = 0;
    u64 duration_tstates = 0;
};

//
// Safe waveform assembler used by both file decoders.
//
class builder {
public:
    static constexpr std::size_t max_events = 4'000'000;

    explicit builder(std::string format, std::size_t image_bytes);

    bool append_span(bool level, u64 duration);
    bool append_pulse(u64 duration);
    bool append_pulses(u64 duration, u64 count);
    bool append_data(std::span<const u8> data, u16 zero_pulse,
                     u16 one_pulse, u8 used_bits_last);
    bool append_pause(u16 milliseconds);
    bool append_stop();
    void set_level(bool level);

    bool good() const;
    const std::string &error() const;
    bool level() const;
    image finish(std::size_t blocks, std::size_t data_blocks,
                 std::string title = {});

private:
    bool fail(std::string message);

    image image_;
    bool level_ = false;
    std::string error_;
};

bool decode_tap(std::span<const u8> data, image &result,
                std::string &error);
bool decode_tzx(std::span<const u8> data, image &result,
                std::string &error);

} // namespace spectrum::tape_internal

#endif // SPECTRUM_TAPE_INTERNAL_H
