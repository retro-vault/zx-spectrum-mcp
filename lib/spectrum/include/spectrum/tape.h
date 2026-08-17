//
// virtual cassette deck for TAP and TZX tape images.
//
// Tape images are decoded into level spans measured in 3.5 MHz T-states.
// The deck advances only with the emulated machine clock and presents its
// current level to the ULA EAR input, so ROM and custom loaders observe the
// same edges they would receive from a cassette recorder.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_TAPE_H
#define SPECTRUM_TAPE_H

#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include "spectrum/types.h"

namespace spectrum {

//
// Current cassette image and transport state.
//
struct tape_status {
    bool loaded = false;
    bool playing = false;
    bool finished = false;
    bool stopped_by_command = false;
    bool ear_level = false;
    std::string format;
    std::string title;
    std::size_t image_bytes = 0;
    std::size_t blocks = 0;
    std::size_t data_blocks = 0;
    std::size_t segments = 0;
    std::size_t segment = 0;
    u64 duration_tstates = 0;
    u64 position_tstates = 0;
};

//
// A tape transport whose time base is the Spectrum master clock.
//
class tape_deck {
public:
    tape_deck();
    ~tape_deck();

    tape_deck(const tape_deck &) = delete;
    tape_deck &operator=(const tape_deck &) = delete;

    //
    // Decode and insert a TAP image.
    //
    // Parameters:
    //      data        - complete TAP file bytes.
    //      autoplay    - start the transport after inserting it.
    //      error       - receives a validation error on failure.
    //
    // Returns:
    //      true when the complete image was accepted.
    //
    bool insert_tap(std::span<const u8> data, bool autoplay,
                    std::string &error);

    // Decode and insert a TZX image.
    bool insert_tzx(std::span<const u8> data, bool autoplay,
                    std::string &error);

    // Start or resume playback. Returns false with no tape or at its end.
    bool play();

    // Stop at the current tape position.
    void stop();

    // Return to the beginning and leave the transport stopped.
    void rewind();

    // Remove the current image.
    void eject();

    // Rewind and stop as part of a machine reset, preserving the image.
    void reset();

    // Advance playback by a number of emulated T-states.
    void tick(u64 tstates = 1);

    // Return the signal currently connected to ULA EAR.
    bool ear_level() const;

    // Return image, position and transport diagnostics.
    tape_status status() const;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

} // namespace spectrum

#endif // SPECTRUM_TAPE_H
