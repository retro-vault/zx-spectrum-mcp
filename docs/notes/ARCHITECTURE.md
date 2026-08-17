# Architecture

Design notes for `zx-spectrum-mcp`: a cycle-accurate ZX Spectrum 48K
emulator with no display, driven entirely through the Model Context
Protocol over stdin and stdout.

This document records *why* things are the way they are. For how to use
the server, see [`docs/manuals/USER-GUIDE.md`](../manuals/USER-GUIDE.md).

---

## 1. The shape of the thing

```
                     stdin  ──────────────┐
                                          ▼
                            ┌─────────────────────────┐
                            │  mcp::stdio_transport   │  line-delimited JSON
                            └────────────┬────────────┘
                                         ▼
                            ┌─────────────────────────┐
                            │      mcp::server        │  parse / dispatch
                            └────────────┬────────────┘
                                         ▼
              chain of responsibility over JSON-RPC methods
    initialize → initialized → tools/list → tools/call → ping → fallback
                                         │
                                         ▼
                            ┌─────────────────────────┐
                            │   mcp::tool_registry    │  15 command objects
                            └────────────┬────────────┘
                                         ▼
                            ┌─────────────────────────┐
                            │   spectrum::machine     │  facade + master clock
                            └────────────┬────────────┘
             ┌──────────────┬────────────┼────────────┬──────────────┐
             ▼              ▼            ▼            ▼              ▼
           cpu           memory         ula        io_bus      breakpoint_set
        (Z80 core)    (16K + 48K)   (video +      (chain)
                                     port 0xFE)
                                         │
                                         ▼
                                     keyboard
```

Four libraries under `lib/`, none of which depends on the one above it:

| Library    | Depends on   | Responsibility                        |
|------------|--------------|---------------------------------------|
| `json`     | —            | RFC 8259 parse and serialise          |
| `png`      | `miniz`      | 4-bit indexed PNG writer              |
| `spectrum` | `z80`        | the emulated machine                  |
| `mcp`      | `json`       | JSON-RPC 2.0 and MCP methods          |

`src/` wires them together. The emulator knows nothing about MCP, and
the MCP layer knows nothing about the Spectrum: the only place the two
meet is in the tool classes.

---

## 2. Cycle accuracy

This is the part everything else is arranged around.

### 2.1 Why the CPU core had to be tick-stepped

Most Z80 cores are *instruction-stepped*: they execute a whole
instruction and report how many T-states it took. That is unusable here,
because on a real 48K the ULA stalls the CPU **in the middle of a
machine cycle**, and how long for depends on where the electron beam is
at that instant. You cannot reconstruct that after the fact.

`chips/z80.h` is *tick-stepped*. One `z80_tick()` call advances the CPU
by exactly one T-state and exchanges a 64-bit pin mask carrying the
address bus, data bus and control lines. That gives two things:

1. The ULA can be clocked in lockstep, one T-state at a time.
2. Every machine cycle announces itself, so contention can be charged to
   the cycle that caused it.

See [`lib/z80/UPSTREAM.md`](../../lib/z80/UPSTREAM.md) for the full
comparison against the alternatives.

### 2.2 The WAIT pin does not work for this, and we do not use it

The obvious design is to drive contention through the core's `/WAIT`
pin, which it implements faithfully:

```c
#define _wait()  {if(pins&Z80_WAIT)goto step_to;}
```

**It cannot be made to work.** For every machine cycle *except* the
opcode fetch, the core evaluates `_wait()` *before* it puts the address
on the bus. The decision has to be made one T-state before the
information needed to make it exists.

This was established by measurement, not by reading the code. Injecting
one wait state on the rising edge of `MREQ`:

| Instruction | Base | With 1 wait per bus cycle | Bus cycles | Honoured |
|-------------|------|---------------------------|-----------|----------|
| `NOP`       | 4    | 5                         | 1         | yes      |
| `LD BC,nn`  | 10   | 11                        | 3         | only M1  |
| `LD A,(HL)` | 7    | 8                         | 2         | only M1  |
| `PUSH BC`   | 11   | 12                        | 3         | only M1  |

Only the opcode fetch ever honoured the pin.

**What we do instead**: `cpu::tick()` returns the number of T-states the
bus imposed, and `machine::advance_clock()` runs the ULA that many
T-states *without ticking the CPU*. That is precisely what `/WAIT` does
physically — the processor is held off the bus while the video circuitry
carries on — and it puts the decision where the address is known.

### 2.3 Correcting when the address appears

The core presents its address bus a fixed number of T-states after the
real chip would. Measured by logging every assertion against
instructions with known machine cycle boundaries:

```
NOP        ( 4T)  1:M1@8000  3:RFSH
LD BC,nn   (10T)  1:M1@8000  3:RFSH  6:MEM@8001   9:MEM@8002
LD A,(HL)  ( 7T)  1:M1@8000  3:RFSH  6:MEM@FFFF
IN A,(n)   (11T)  1:M1@8000  3:RFSH  6:MEM@8001  10:IO@FFFE
OUT (n),A  (11T)  1:M1@8000  3:RFSH  6:MEM@8001   9:IO@FFFE
PUSH BC    (11T)  1:M1@8000  3:RFSH  7:MEM@FFFE  10:MEM@FFFD
CALL nn    (17T)  1:M1@8000  3:RFSH  6:MEM@8001   9:MEM@8002 …
```

A real Z80 asserts in T1 of each machine cycle, so `LD BC,nn` has cycles
beginning at T1, T5 and T8. Comparing gives a constant offset per cycle
type, confirmed across prefixed and repeating instructions:

| Machine cycle       | Lag | Constant in `cpu.cpp` |
|---------------------|-----|-----------------------|
| Opcode fetch (M1)   | 0   | `opcode_fetch_lag`    |
| Memory read / write | +1  | `memory_lag`          |
| I/O write (`OUT`)   | +1  | `io_write_lag`        |
| I/O read (`IN`)     | +2  | `io_read_lag`         |

`bus_interface` receives this lag alongside the address, so `machine`
samples the contention table at the T-state the real ULA would have.
`tests/test_cpu_timing.cpp` pins it down: `LD A,(0x4000)` from
uncontended code must take exactly `13 + table[start_of_final_cycle]`.
A wrong offset samples a neighbouring entry and the test fails.

Refresh cycles assert `MREQ` too, but with `RFSH` set. They are skipped:
the ULA does not contend on refresh.

### 2.4 The contention model

48K geometry, all in `zx_spectrum_48k_timing`:

- 3.5 MHz, 224 T-states per line, 312 lines, **69888 T-states per frame**
- `/INT` asserted at T-state 0 for 32 T-states
- first display pixel drawn at T-state **14336** = line 64, column 0
- contention begins at **14335**, one T-state earlier, because the ULA
  fetches ahead of the beam

`ula_contention` precomputes one delay per T-state of the frame. Across
each 8 T-state block of a display line the pattern is:

```
offset   0  1  2  3  4  5  6  7
delay    6  5  4  3  2  1  0  0
```

The ULA needs six of every eight T-states to fetch two bitmap bytes and
two attribute bytes — the next sixteen pixels — and releases the bus for
the other two.

I/O contention follows the four-case rule, walked step by step rather
than summed, because each contended step can push the next into a
different block:

| High byte in 0x40–0x7F | A0 low | Pattern              |
|------------------------|--------|----------------------|
| no                     | no     | `N:4`                |
| no                     | yes    | `N:1 C:3`            |
| yes                    | no     | `C:1 C:1 C:1 C:1`    |
| yes                    | yes    | `C:1 C:3`            |

`contention_model` is an interface, so a 128K or +2A/+3 model is a new
implementation rather than a new code path.

### 2.5 Beam-synchronous rendering

The ULA is ticked once per T-state and paints the two pixels the beam
covers. **Nothing is deferred to the end of the frame.** A program that
changes the border or rewrites the display file mid-frame gets exactly
the picture the hardware would produce, because the pixels above the
beam were painted already and cannot be retroactively changed.

`raster_map` precomputes, for every T-state, where the beam is and
whether it is painting border, display or nothing. That table exists
because the mapping is not obvious: a line is transmitted as

```
columns   0..127   256-pixel display
columns 128..151   right border
columns 152..199   horizontal retrace  (paints nothing)
columns 200..223   left border of the *following* row
```

so the tail of a line already belongs to the next visible row, and
`t / tstates_per_line` is the wrong answer.

Display bytes are latched in 8 T-state blocks, matching the real fetch
cycle: two bitmap bytes and two attributes covering sixteen pixels. The
fetch schedule and the contention table are two descriptions of the same
piece of hardware.

**What this buys.** `tests/test_display.cpp` runs a border-cycling loop
on the emulated Z80 and asserts:

- 8 distinct border colours in a single frame
- 280 of 288 rows have a uniform left border, so the bands are horizontal
- the same program paints a pixel-identical frame on a second run
- **running the identical loop from contended memory moves the stripes
  by 212 rows** — contention genuinely reaches the CPU

It also checks every border pixel against a table of paint times, so the
assertion is "this pixel has the colour that was set when the beam
crossed it", not a sample of two points.

### 2.6 The overlapped instruction boundary

`z80_opdone()` is true when the *next* instruction's opcode fetch has
happened:

```c
return ((cpu->pins & (Z80_M1|Z80_RD)) == (Z80_M1|Z80_RD)) && !cpu->prefix_active;
```

Two consequences, both of which caused real bugs during development:

1. **Timing is measured boundary to boundary.** From one boundary to the
   next is exactly the instruction's published length. Measuring from a
   non-boundary reads one T-state high.

2. **The core's `pc` is not the program counter a debugger wants.**
   `_z80_fetch` does `pc++`, so at a boundary `pc` already points *past*
   the opcode about to execute. `machine` therefore tracks the address
   of each opcode fetch and reports that as `pc`.

   Only fetches that *start* an instruction count. The second opcode
   byte of a `DD`/`FD`/`ED`/`CB` instruction arrives through the same
   path, and recording it reported the middle of an instruction whenever
   a run stopped on a T-state or frame limit. `instruction_complete()`
   is false while a prefix is being resolved, which distinguishes them.

---

## 3. Patterns, and why each one is there

Every pattern below earns its place by removing a `switch` or a
dependency. None is decorative.

| Pattern | Where | What it buys |
|---|---|---|
| **Chain of responsibility** | `mcp::request_handler` | Each JSON-RPC method is a link. The server contains no dispatch logic; the chain ends in `fallback_handler`, so it can never run off the end. |
| **Chain of responsibility** | `spectrum::io_bus` | Spectrum port decoding is *partial* — the ULA answers every even port. A device cannot be registered against a port number, only asked whether it recognises an address. Attachment order is priority order, as edge-connector hardware behaves. |
| **Template method** | `request_handler::handle()` | Non-virtual traversal; subclasses supply `can_handle()` and `process()` only, so no handler can break the walk. |
| **Command** | `mcp::tool` | Each tool knows its name, schema and how to run itself. Adding one is a new class and one `registry.add()` line. |
| **Strategy** | `contention_model` | 48K contention, or `no_contention` for isolating the CPU in tests. A 128K model is a new class. |
| **Adapter** | `spectrum::cpu` | Wraps the C core's pin protocol behind `bus_interface`. |
| **Bridge (pimpl)** | `cpu::impl` | Keeps `z80.h` out of every other translation unit — see §4. |
| **Facade** | `spectrum::machine` | One object owning CPU, memory, ULA, keyboard and bus, and running the master clock. |
| **Registry** | `mcp::tool_registry` | Owns the tools; listing order is registration order, so `tools/list` is stable. |
| **Null object** | `json::value` lookups | A missing key returns a shared immutable null, so `msg["params"]["addr"].as_int()` is safe on absent fields without a single `try`. |

### The keyboard is deliberately *not* an `io_device`

On real hardware the keyboard is not a peripheral on the bus at all: it
is a passive matrix of switches wired into the ULA, and the ULA decodes
port 0xFE and merges the five key bits with the tape input. Modelling it
as a device would put two objects on the same address fighting over it.
`ula` owns port 0xFE and reads `keyboard` as a component.

---

## 4. Notable decisions

**`z80.h` lives in exactly one translation unit.** Upstream says so
itself: *"These unions are fine in C, but not C++."* The register file
uses anonymous unions containing anonymous structs, which is a GCC
extension in C++ and warns under `-pedantic`. `cpu.cpp` includes it
behind a scoped `#pragma GCC diagnostic`, and the pimpl keeps it out of
everything else. The rest of the project compiles clean under
`-Wall -Wextra -pedantic`.

**JSON is hand-written rather than vendored.** The protocol needs a
narrow, strict subset, and integers must round-trip exactly — T-state
counters outgrow the 2^53 a double can hold. `json::value` stores
`int64_t` and `double` as separate alternatives. Object members keep
insertion order so the wire format is stable and diffable. Parse
failures are returned, never thrown, because a bad message is a normal
protocol event that must be answered with a JSON-RPC parse error rather
than unwinding the server loop.

**PNG is 4-bit indexed.** The machine has sixteen colours, so a byte per
pixel loses nothing and the encoder emits half a byte. A screenshot
travels base64-encoded inside an MCP response and ends up in a model's
context window; compressed, a typical screen is one to three KB against
about 50 KB raw.

**miniz is vendored rather than linking system zlib.** The build is
statically linked and must work on a machine without `zlib1g-dev`.

**ROMs are explicit inputs.** Images are kept under `data/roms/`, but
`memory` still boots a small stub unless `--rom` is passed. That keeps
tests and machine-code use deterministic without silently depending on
a copyrighted image. `--interface1-rom` likewise opts into the shadow
ROM and its paging traps.

**`screen_text` reads the font out of memory** via the `CHARS` system
variable rather than using a built-in table. That avoids shipping the
copyrighted font *and* means a program that redefines the character set
is still read correctly. `font_usable` reports when there is nothing
font-shaped at the pointer.

**Every run is bounded.** `machine::run()` refuses to run without a
limit — an unbounded call defaults to one frame — and the tools cap a
single call at about twenty seconds of emulated time. A server on the
end of a pipe cannot be allowed to hang on a wedged program.

**Tape is a signal, not a file-loading trap.** TAP and TZX decoders compile
an image into constant-level spans measured in the Spectrum's 3.5 MHz
T-states. The cassette transport advances from the same master clock as the
CPU and ULA, and ULA port `$FE` samples its current level on EAR bit 6. This
keeps ROM and custom loader loops observable and preserves turbo, sampled,
generalized, pause and control-flow timing without coupling the tape decoder
to Z80 code or RAM layout.

**Execution breakpoints stop before the instruction; data breakpoints
stop after it.** An execute breakpoint is judged at the boundary, so the
machine stops with `pc` still naming the instruction. A memory or I/O
breakpoint fires mid-instruction and is deferred to the end of it, so
the machine is never left half way through an opcode.

---

## 5. What is not implemented

Honest list of the edges.

- **128K, +2, +3** — no paging, no AY sound, no second contention model.
  `contention_model` and `io_device` are the seams where they would go.
- **Sound** — the speaker and tape-output bits of port 0xFE are decoded
  and readable, but no audio is generated. A display-less, audio-less
  server has nowhere to put it.
- **Disassembly** — `read_memory` returns bytes. A Z80 disassembler
  would make `step` far more useful and is the most obvious next tool.
- **Floating bus** is approximated: the ULA drives the bus during the
  first four T-states of each fetch block and reads 0xFF otherwise.
  Right for the common beam-sync idiom, not verified against hardware
  edge cases.
- **I/O contention placement.** The total stall is exact and follows the
  four-case rule; *where* inside the four T-state cycle each part lands
  is not separately modelled, because software measures the total.
- **`memory::peek`/`poke` are the same as `read`/`write` today.** They
  are kept apart so a future banked model can expose a physical view
  without disturbing the CPU path.

---

## 6. Testing

`make test` builds everything with `-fsanitize=address,undefined` and
runs twelve C++ suites — 509 checks plus 1,356 external conformance
vectors. A test that passes while corrupting memory is not a passing
test.

| Suite | Covers |
|---|---|
| `test_z80_vectors` | all 1,356 pinned Fuse Z80 core vectors: registers, flags, MEMPTR/WZ, memory, I/O, HALT and exact timing; focused multi-instruction Q-latch checks |
| `test_cpu_timing` | 25 published instruction timings; contention applied to real instructions; `pc` reporting through the overlapped fetch and prefixed instructions; HALT and the frame interrupt |
| `test_contention` | the 6,5,4,3,2,1,0,0 pattern, its boundaries, contended address range, the four I/O cases |
| `test_display` | raster geometry, attribute and flash rendering, **border raster effects**, contention shifting them, text recognition, ASCII art |
| `test_json` | scalars, 64-bit integer precision, escapes and surrogate pairs, insertion order, 19 malformed inputs, depth limit |
| `test_keyboard` | matrix bit layout, name aliases, character translation, port 0xFE both ways |
| `test_snapshot` | binary, `.scr`, `.sna`, `.z80` v1 and v2, RLE decoding, 128K rejection |
| `test_tape` | TAP ROM timing; TZX standard/turbo/pure/direct, CSW/Z-RLE and generalized data; EAR integration; stops, loops, jumps, calls, selection and malformed images |
| `test_png` | chunk structure, every CRC recomputed, IDAT inflated and compared pixel by pixel, scaling, rejections |
| `test_capture` | persistent PNG output; exact-rate YUV4MPEG headers, frame boundaries, size, start/stop errors and repeated recordings |
| `test_serial` | Interface 1 port decode and ROM byte framing, Z80-driven output, shadow-ROM traps, live two-port TCP data/control behavior |
| `test_mcp` | handshake and version negotiation, notifications never answered, `tools/list` schemas, tool invocation, error taxonomy, malformed input, a full session over a transport |

The distinction the MCP suite guards most carefully: **a tool that
cannot do what was asked returns `isError`** so the model can read it and
retry, while **a malformed call returns a JSON-RPC error** because the
caller used the protocol wrongly.
