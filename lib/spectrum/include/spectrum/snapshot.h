//
// loading programs and machine states into the emulator.
//
// six formats are supported, covering programs, states, screens and tape:
// driving a machine from a script:
//
//   binary  raw bytes placed at an address, optionally jumped to. this
//           is what an assembler produces and what most testing needs.
//   scr     a 6912 byte display file dump. loads a picture without
//           running anything, handy for checking the renderer.
//   sna     the original 48K snapshot. a complete machine state; the
//           program counter is recovered by popping it off the stack,
//           which is how the format was designed to be restored.
//   z80     versions 1, 2 and 3, the format almost every archived
//           program is distributed in, including its run length
//           encoding.
//
//   tap     raw Spectrum ROM-format tape blocks, played as timed EAR pulses.
//   tzx     timing-preserving tape images, including control-flow and
//           sampled/generalized data blocks used by custom loaders.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_SNAPSHOT_H
#define SPECTRUM_SNAPSHOT_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "spectrum/machine.h"
#include "spectrum/types.h"

namespace spectrum {

//
// the formats load_into() understands.
//
enum class snapshot_format : u8 {
    // raw bytes at a chosen address.
    binary,

    // 6912 byte display file dump.
    scr,

    // 48K .sna machine state.
    sna,

    // .z80 machine state, versions 1 to 3.
    z80,

    // Spectrum tape blocks with standard ROM timings.
    tap,

    // Extended tape image with per-block timings and control flow.
    tzx,
};

//
// Returns: the wire name of a format, such as "z80".
//
const char *snapshot_format_name(snapshot_format format);

//
// Parse a format from its wire name, case insensitively.
//
std::optional<snapshot_format> snapshot_format_from_name(
    std::string_view name);

//
// Guess a format from a filename extension.
//
// Returns:
//      the format, or nullopt when the extension is not recognised.
//
std::optional<snapshot_format> snapshot_format_from_path(
    std::string_view path);

//
// how to load.
//
struct load_options {
    snapshot_format format = snapshot_format::binary;

    // where raw bytes go. ignored by every other format.
    u16 address = 0;

    // for binary loads, set the program counter here afterwards.
    std::optional<u16> start;

    // wipe RAM and reset the CPU before loading. machine states always
    // reset regardless, because a partial state is meaningless.
    bool reset_first = false;

    // Start cassette playback after inserting TAP or TZX. Ignored by
    // memory and snapshot formats.
    bool autoplay = true;
};

//
// what happened.
//
struct load_result {
    bool ok = false;

    // why it failed, empty on success.
    std::string error;

    // bytes of the image actually consumed.
    std::size_t bytes = 0;

    // where execution will continue.
    u16 entry = 0;

    // human readable summary for the tool response.
    std::string description;
};

//
// Load an image into a machine.
//
// Parameters:
//      target      - the machine to load into.
//      data        - the raw file contents.
//      options     - format and placement.
//
// Returns:
//      the outcome. on failure the machine is left untouched where
//      possible, but a truncated machine state may leave it reset.
//
// Sample call:
//      spectrum::load_options o;
//      o.format = spectrum::snapshot_format::z80;
//      auto r = spectrum::load_into(m, bytes, o);
//
load_result load_into(machine &target, std::span<const u8> data,
                      const load_options &options);

//
// Load a .z80 snapshot. exposed for tests; load_into() dispatches here.
//
load_result load_z80_snapshot(machine &target, std::span<const u8> data);

//
// Read a whole file.
//
// Parameters:
//      path        - filesystem path.
//      error       - set to a message when the read fails.
//
// Returns:
//      the contents, or nullopt on failure.
//
std::optional<std::vector<u8>> read_file(const std::string &path,
                                         std::string &error);

//
// Decode the run length encoding used inside .z80 files.
//
// Parameters:
//      source      - the compressed bytes.
//      stop_at_end - honour the 00 ED ED 00 end marker, as version 1
//                    files use. version 2 and 3 blocks carry an
//                    explicit length instead.
//
// Returns:
//      the expanded bytes.
//
// Notes:
//      the encoding replaces five or more equal bytes with
//      ED ED count value. exposed because it is worth testing directly.
//
std::vector<u8> decompress_z80_block(std::span<const u8> source,
                                     bool stop_at_end);

} // namespace spectrum

#endif // SPECTRUM_SNAPSHOT_H
