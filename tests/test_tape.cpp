//
// TAP/TZX decoding, exact EAR timing, transport and MCP cassette control.
//
// This remains a standalone C++ suite, matching every other project test.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include <string>
#include <vector>

#include "mcp/tool_registry.h"
#include "spectrum/machine.h"
#include "spectrum/snapshot.h"
#include "test_support.h"
#include "tools/registration.h"

namespace {

using spectrum::u8;
using spectrum::u16;
using spectrum::u32;

void word(std::vector<u8> &out, u16 value)
{
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
}

void three(std::vector<u8> &out, u32 value)
{
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value >> 16));
}

void dword(std::vector<u8> &out, u32 value)
{
    three(out, value);
    out.push_back(static_cast<u8>(value >> 24));
}

std::vector<u8> tzx()
{
    return {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1a, 1, 20};
}

void pure_tone(std::vector<u8> &out, u16 duration, u16 count)
{
    out.push_back(0x12);
    word(out, duration);
    word(out, count);
}

bool insert_tzx(spectrum::tape_deck &deck, const std::vector<u8> &image,
                bool autoplay = true)
{
    std::string error;
    const bool ok = deck.insert_tzx(image, autoplay, error);
    if (!ok)
        test::check(false, "TZX fixture decodes: " + error);
    return ok;
}

void test_tap()
{
    test::section("TAP ROM waveform and validation");

    spectrum::tape_deck deck;
    std::string error;
    const std::vector<u8> image = {1, 0, 0x80};
    test::check(deck.insert_tap(image, true, error),
                "a standard data block is accepted");

    const spectrum::tape_status loaded = deck.status();
    test::check_eq_str(loaded.format, "tap", "the format is reported");
    test::check_eq(loaded.blocks, 1, "one TAP block is present");
    test::check_eq(loaded.duration_tstates, 10'504'256,
                   "ROM pilot, sync, data and pause timing is exact");
    test::check(!deck.ear_level(), "the standard pilot begins low");
    deck.tick(2168);
    test::check(deck.ear_level(), "the EAR level changes on the pilot edge");
    deck.stop();
    test::check(!deck.ear_level(), "a stopped deck drives silence");
    test::check(deck.play(), "play resumes at the same tape position");
    test::check(deck.ear_level(), "the resumed signal keeps its level");

    const std::vector<u8> truncated = {2, 0, 0x00};
    test::check(!deck.insert_tap(truncated, true, error),
                "a truncated TAP block is rejected");
    test::check_contains(error, "truncated", "the TAP error is explicit");
    test::check_eq(deck.status().blocks, 1,
                   "a bad insert leaves the old tape intact");

    deck.eject();
    test::check(!deck.status().loaded, "eject removes the tape");
    test::check(!deck.status().finished,
                "an empty transport is not reported as finished media");
}

void test_basic_tzx_blocks()
{
    test::section("TZX pulse and data blocks");

    spectrum::tape_deck deck;
    std::vector<u8> image = tzx();
    pure_tone(image, 10, 3);
    test::check(insert_tzx(deck, image), "pure tone loads");
    test::check_eq(deck.status().duration_tstates, 30,
                   "pure-tone duration is exact");
    deck.tick(10);
    test::check(deck.ear_level(), "pure-tone pulses alternate");
    deck.tick(20);
    test::check(deck.status().finished, "playback finishes at its exact end");

    image = tzx();
    image.push_back(0x13);
    image.push_back(2);
    word(image, 7);
    word(image, 8);
    image.push_back(0x14);
    word(image, 5);
    word(image, 6);
    image.push_back(1);
    word(image, 0);
    three(image, 1);
    image.push_back(0x80);
    test::check(insert_tzx(deck, image), "pulse sequence and pure data load");
    test::check_eq(deck.status().duration_tstates, 27,
                   "pulse-sequence and one-bit data timings compose");

    image = tzx();
    image.push_back(0x11);
    word(image, 2);  // pilot
    word(image, 3);  // sync 1
    word(image, 4);  // sync 2
    word(image, 5);  // zero
    word(image, 6);  // one
    word(image, 2);  // pilot count
    image.push_back(8);
    word(image, 0);
    three(image, 1);
    image.push_back(0x80);
    test::check(insert_tzx(deck, image), "a turbo data block loads");
    test::check_eq(deck.status().duration_tstates, 93,
                   "turbo timing fields control every pulse");

    image = tzx();
    image.push_back(0x10);
    word(image, 0);
    word(image, 1);
    image.push_back(0x80);
    test::check(insert_tzx(deck, image), "a standard-speed TZX block loads");
    test::check_eq(deck.status().duration_tstates, 7'004'256,
                   "standard-speed TZX uses Spectrum ROM timing");
}

void test_direct_and_machine_ear()
{
    test::section("direct recording and ULA EAR input");

    spectrum::tape_deck deck;
    std::vector<u8> image = tzx();
    image.push_back(0x15);
    word(image, 5);
    word(image, 0);
    image.push_back(4);
    three(image, 1);
    image.push_back(0xd0); // high, high, low, high
    test::check(insert_tzx(deck, image), "direct recording loads");
    test::check(deck.ear_level(), "absolute high sample is visible first");
    deck.tick(10);
    test::check(!deck.ear_level(), "identical samples merge without drifting");
    deck.tick(5);
    test::check(deck.ear_level(), "the final high sample follows");
    deck.tick(5);
    test::check(deck.status().finished, "all direct samples are consumed");

    spectrum::machine target;
    image = tzx();
    image.push_back(0x2b);
    dword(image, 1);
    image.push_back(1);
    pure_tone(image, 5, 1);
    std::string error;
    test::check(target.tape().insert_tzx(image, true, error),
                "signal-level tape inserts into a machine");
    test::check((target.read_port(0xfefe) & 0x40) != 0,
                "ULA port bit 6 reads the tape EAR high level");
    target.run_tstates(5);
    test::check((target.read_port(0xfefe) & 0x40) == 0,
                "machine T-states advance and finish the tape waveform");
}

void test_tzx_transport_and_control_flow()
{
    test::section("TZX stops, loops, jumps, calls and selection");

    spectrum::tape_deck deck;
    std::vector<u8> image = tzx();
    image.push_back(0x20);
    word(image, 0);
    pure_tone(image, 10, 1);
    test::check(insert_tzx(deck, image), "stop-command image loads");
    test::check(deck.status().stopped_by_command,
                "autoplay obeys a TZX stop command");
    test::check(deck.play(), "play resumes beyond the stop command");
    deck.tick(10);
    test::check(deck.status().finished, "resumed playback reaches the end");

    image = tzx();
    image.push_back(0x24);
    word(image, 3);
    pure_tone(image, 10, 1);
    image.push_back(0x25);
    test::check(insert_tzx(deck, image), "loop image loads");
    test::check_eq(deck.status().duration_tstates, 30,
                   "loop bodies expand the requested number of times");

    image = tzx();
    image.push_back(0x23);
    word(image, 2);
    pure_tone(image, 99, 1);
    pure_tone(image, 7, 2);
    test::check(insert_tzx(deck, image), "relative jump image loads");
    test::check_eq(deck.status().duration_tstates, 14,
                   "a relative jump skips the unwanted block");

    image = tzx();
    image.push_back(0x26);
    word(image, 2);
    word(image, 3);
    word(image, 5);
    image.push_back(0x23);
    word(image, 6);
    image.push_back(0x30);
    image.push_back(0);
    pure_tone(image, 10, 1);
    image.push_back(0x27);
    pure_tone(image, 20, 1);
    image.push_back(0x27);
    pure_tone(image, 30, 1);
    test::check(insert_tzx(deck, image), "call-sequence image loads");
    test::check_eq(deck.status().duration_tstates, 60,
                   "each called sequence returns before main playback");

    image = tzx();
    image.push_back(0x28);
    word(image, 5);
    image.push_back(1);
    word(image, 2);
    image.push_back(1);
    image.push_back('A');
    pure_tone(image, 99, 1);
    pure_tone(image, 8, 1);
    test::check(insert_tzx(deck, image), "selection image loads");
    test::check_eq(deck.status().duration_tstates, 8,
                   "headless selection takes the first choice");
}

std::vector<u8> csw_image(u8 compression)
{
    const std::vector<u8> raw = {1, 2};
    std::vector<u8> encoded = raw;
    if (compression == 2) {
        encoded.resize(mz_compressBound(raw.size()));
        mz_ulong size = static_cast<mz_ulong>(encoded.size());
        const int status = mz_compress2(encoded.data(), &size, raw.data(),
                                        raw.size(), MZ_BEST_COMPRESSION);
        if (status != MZ_OK)
            encoded.clear();
        else
            encoded.resize(size);
    }

    std::vector<u8> image = tzx();
    image.push_back(0x18);
    dword(image, static_cast<u32>(10 + encoded.size()));
    word(image, 0);
    three(image, 3'500'000);
    image.push_back(compression);
    dword(image, 2);
    image.insert(image.end(), encoded.begin(), encoded.end());
    return image;
}

void test_extended_tzx_blocks()
{
    test::section("CSW and generalized TZX data");

    spectrum::tape_deck deck;
    std::vector<u8> image = csw_image(1);
    test::check(insert_tzx(deck, image), "RLE CSW recording loads");
    test::check_eq(deck.status().duration_tstates, 3,
                   "CSW samples convert to 3.5 MHz T-states");

    image = csw_image(2);
    test::check(insert_tzx(deck, image), "Z-RLE CSW recording inflates");
    test::check_eq(deck.status().duration_tstates, 3,
                   "compressed CSW preserves pulse timing");

    std::vector<u8> payload;
    word(payload, 0); // pause
    dword(payload, 1); // one pilot RLE record
    payload.push_back(1); // pilot pulses per symbol
    payload.push_back(1); // pilot alphabet size
    dword(payload, 2); // packed data symbols
    payload.push_back(1); // data pulses per symbol
    payload.push_back(2); // data alphabet size
    payload.push_back(0);
    word(payload, 10); // pilot symbol table
    payload.push_back(0);
    word(payload, 2); // symbol 0, repeat twice
    payload.push_back(0);
    word(payload, 20); // data symbol 0
    payload.push_back(0);
    word(payload, 30); // data symbol 1
    payload.push_back(0x40); // symbols 0, 1 packed MSB first

    image = tzx();
    image.push_back(0x19);
    dword(image, static_cast<u32>(payload.size()));
    image.insert(image.end(), payload.begin(), payload.end());
    test::check(insert_tzx(deck, image), "generalized data loads");
    test::check_eq(deck.status().duration_tstates, 70,
                   "pilot RLE and packed generalized symbols are decoded");
}

void test_loading_and_tape_tool()
{
    test::section("format dispatch and cassette command");

    spectrum::machine target;
    mcp::tool_registry registry;
    tools::register_load_tool(registry, target);
    tools::register_tape_tools(registry, target);
    mcp::tool *loader = registry.find("load");
    mcp::tool *command = registry.find("tape");
    test::check(loader != nullptr && command != nullptr,
                "the load and tape MCP commands are registered");
    if (!loader || !command)
        return;

    json::value load = json::value::make_object();
    load.set("data", json::value("010080"));
    load.set("format", json::value("tap"));
    load.set("autoplay", json::value(false));
    const mcp::tool_result loaded = loader->invoke(load);
    test::check(!loaded.is_error, "the load command dispatches TAP images");
    test::check(!target.tape().status().playing,
                "the load autoplay option is honoured");

    json::value play = json::value::make_object();
    play.set("action", json::value("play"));
    const mcp::tool_result result = command->invoke(play);
    test::check(!result.is_error, "the command starts playback");
    test::check(result.structured["playing"].as_bool(false),
                "transport state is returned structurally");

    std::vector<u8> bad = tzx();
    bad.push_back(0x99);
    std::string error;
    test::check(!target.tape().insert_tzx(bad, true, error),
                "an unknown TZX block is rejected");
    test::check_contains(error, "unknown", "unsupported data is diagnosed");
    bad = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1a, 2, 0};
    test::check(!target.tape().insert_tzx(bad, true, error),
                "a future major TZX version is rejected");
}

} // namespace

int main()
{
    test_tap();
    test_basic_tzx_blocks();
    test_direct_and_machine_ear();
    test_tzx_transport_and_control_flow();
    test_extended_tzx_blocks();
    test_loading_and_tape_tool();
    return test::summary("tape");
}
