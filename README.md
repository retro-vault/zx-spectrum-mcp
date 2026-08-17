# zx-spectrum-mcp

A **cycle-accurate ZX Spectrum 48K emulator** with no display, driven
entirely through the **Model Context Protocol** over stdin and stdout.

It emulates the Z80 and the ULA one T-state at a time, including memory
contention and beam-synchronous video, so timing-dependent effects work
the way they do on real hardware — border raster stripes included. You
load a program, run it, read the screen back, save PNG screenshots, or
record exact emulated frames to a video file.

```
$ printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"status","arguments":{}}}' \
  | bin/bin/zx-spectrum-mcp
```

> ZX Spectrum 48K at 3500000 Hz, 69888 T-states per frame (224x312
> lines), contention model 'ula-48k'. No ROM loaded, running the
> built-in stub. Elapsed 0 T-states over 0 frames; pc is 0x0000.

---

## Build

Needs GCC with C++20 (12 or newer) and GNU make. Nothing else — the Z80
core and the DEFLATE compressor are vendored under `lib/`.

```sh
make          # release build, statically linked → bin/bin/zx-spectrum-mcp
make test     # 509 checks plus 1,356 vectors under ASan and UBSan
make debug    # sanitizer build → bin/bin/zx-spectrum-mcp-debug
make package  # add a copyable bin/ and share/ tree under bin/
make help     # all targets
```

The release binary has no shared library dependencies:

```sh
$ ldd bin/bin/zx-spectrum-mcp
	not a dynamic executable
```

### Unix package layout

`make package` stages a filesystem overlay without changing the host:

```text
bin/
├── bin/zx-spectrum-mcp
└── share/
    ├── doc/zx-spectrum-mcp/
    └── zx-spectrum-mcp/roms/
```

Install that overlay with:

```sh
sudo cp -a bin/bin bin/share /usr/local/
```

For a self-contained `/opt` installation instead:

```sh
make package
sudo mkdir -p /opt/zx-spectrum-mcp
sudo cp -a bin/bin bin/share /opt/zx-spectrum-mcp/
```

The same layout can be installed directly with `sudo make install`; use
`PREFIX=/opt/zx-spectrum-mcp` to select `/opt`. ROMs are immutable program
data, so Unix convention puts them under `share`, while `/var` is reserved
for mutable state. Start an installed copy with, for example:

```sh
/usr/local/bin/zx-spectrum-mcp \
  --rom /usr/local/share/zx-spectrum-mcp/roms/48.rom
```

## Usage

```sh
bin/bin/zx-spectrum-mcp [--rom PATH] [--interface1-rom PATH]
                    [--load PATH] [--serial]
                    [--serial-data-port PORT]
                    [--serial-control-port PORT] [--verbose]
                    [--list-tools] [--version] [--help]
```

Register it with an MCP client:

```sh
claude mcp add spectrum -- /absolute/path/to/bin/bin/zx-spectrum-mcp \
  --rom /absolute/path/to/data/roms/48.rom
```

**stdout carries the protocol and nothing else.** All diagnostics go to
stderr.

### Interface 1 serial over TCP

`--serial` attaches the RS-232 portion of Sinclair's ZX Interface 1 and
opens two non-blocking TCP listeners:

- TCP 6601 is a raw, eight-bit data stream in both directions.
- TCP 6602 is a newline-delimited modem-control connection.

The ports can be changed with `--serial-data-port` and
`--serial-control-port`. The control connection accepts `PING`,
`CTS 0|1|AUTO`, and `DCD 0|1|AUTO`, and publishes `RTS 0|1` and
`DTR 0|1` when the emulated outputs change. This is the same two-port
protocol used by `idp-emu`.

The emulated hardware is not a convenient byte-wide UART: it is the real
Interface 1 arrangement, with mirrored control/status port `$EF` and an
inverted, software-driven serial bit on `$F7`. The byte codec follows the
access sequence used by the Interface 1 ROM. See the
[user guide](docs/manuals/USER-GUIDE.md#interface-1-serial-bridge) for
the exact protocol and line mapping.

For the Interface 1 BASIC extensions, load its 8K shadow ROM too:

```sh
bin/bin/zx-spectrum-mcp --rom data/roms/48.rom \
                    --interface1-rom data/roms/if1-2.rom \
                    --serial
```

The shadow ROM pages at the Interface 1 hardware traps `$0008` and
`$1708`, mirrors through the lower 16K, and unpages after fetching the
opcode at `$0700`.

### ROM images

The locally provided ROM files are under `data/roms/`: `48.rom` is the 16K
Spectrum ROM and `if1-2.rom` is the 8K Interface 1 shadow ROM. ROM
copyright is separate from this project's GPL source licence; check your
right to redistribute those binary images. Without `--rom`, the machine
boots a small built-in stub that is sufficient for running machine code.

## Tools

| | |
|---|---|
| `load` `reset` | get code or a snapshot in |
| `tape` | play, stop, rewind, eject or inspect a cassette image |
| `run` `run_until` `step` `status` | make it go |
| `read_memory` `write_memory` | poke about |
| `registers` `breakpoint` | debug |
| `press_keys` `read_port` `set_port` | keyboard and I/O |
| `screen` `screen_text` `screenshot` | see or save what happened |
| `video_start` `video_stop` | record completed emulated frames |

`load` handles raw binary, `.scr`, `.sna`, `.z80` (v1/v2/v3), `.tap`,
and `.tzx`. Tape images are played as T-state-accurate signal levels on
ULA EAR bit 6, so both ROM and custom loaders read the real waveform.
TZX includes turbo/pure/direct data, CSW and generalized blocks, pauses,
signal levels, stops, loops, jumps, calls, and deterministic first-choice
selection. Use `{"action":"status"}` through the `tape` tool to inspect
the transport.
`screen` returns a PNG; `screen_text` returns a 32×24 character grid or
ASCII art, which is much cheaper to read. `screenshot` writes a PNG to
disk. Video is dependency-free YUV4MPEG2 (`.y4m`) at the exact Spectrum
frame rate and can be played directly or converted with standard video
tools.

Full reference with arguments and worked examples:
**[docs/manuals/USER-GUIDE.md](docs/manuals/USER-GUIDE.md)**

## Accuracy

- 69888 T-states per frame, 224 × 312, 3.5 MHz
- `/INT` asserted at T-state 0 for 32 T-states
- first display pixel at T-state 14336; contention begins at 14335
- ULA contention `6,5,4,3,2,1,0,0` per 8 T-state block over
  `0x4000`–`0x7FFF`, plus the four-case I/O rule
- the ULA is ticked once per T-state and paints the two pixels the beam
  covers — nothing is deferred to the end of the frame

Which is why this works:

```
loop:   out  (0xfe),a       ; new border colour every 166 T-states
        inc  a
        and  7
        ld   b,10
wait:   djnz wait
        jr   loop
```

The border comes back striped, the same frame every time — and moves if
you run the identical loop from contended memory.

The C++ test suite checks 25 published instruction timings, cassette
waveforms and TZX control flow, contention
applied at chosen points in the frame, every border pixel against a
table of paint times, the Interface 1 serial framing and live two-port
TCP protocol, shadow-ROM paging, PNG/video capture, and all 1,356 pinned
Fuse Z80 core vectors.

## Layout

```
include/tools/     tool scaffolding headers
src/               main() and the 19 tools
data/roms/         Spectrum and Interface 1 ROM images
lib/spectrum/      the emulated machine
lib/mcp/           JSON-RPC 2.0 and MCP
lib/json/          RFC 8259 parser and writer
lib/png/           4-bit indexed PNG writer
lib/z80/           vendored Z80 core (zlib)
lib/miniz/         vendored DEFLATE (MIT)
tests/             12 C++ suites, including tape, serial and Fuse vectors
docs/manuals/      user guide
docs/notes/        architecture and design decisions
docs/standards/    the coding standards this follows
```

## Design

Why the WAIT pin could not be used for contention, how the address-bus
timing was measured, and what is deliberately left out:
**[docs/notes/ARCHITECTURE.md](docs/notes/ARCHITECTURE.md)**

## Licence

GPL-3.0 — see [LICENSE](LICENSE).

Vendored third-party code keeps its own licence and is documented in
`lib/z80/UPSTREAM.md` (zlib) and `lib/miniz/UPSTREAM.md` (MIT). Both are
GPL-3.0 compatible.
