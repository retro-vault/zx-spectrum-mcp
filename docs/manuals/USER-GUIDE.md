# zx-spectrum-mcp — User Guide

A cycle-accurate ZX Spectrum 48K emulator that speaks the **Model
Context Protocol** over stdin and stdout. It has no window and no
display; you drive it entirely by calling tools, and you can read the
screen, save a PNG, or record completed frames as video.

Because it is cycle accurate — including ULA memory contention and
beam-synchronous rendering — timing-dependent effects such as border
raster stripes work exactly as they do on real hardware.

---

## Contents

1. [Building](#1-building)
2. [Running it](#2-running-it)
3. [Connecting a client](#3-connecting-a-client)
4. [The protocol in five minutes](#4-the-protocol-in-five-minutes)
5. [Tool reference](#5-tool-reference)
6. [Worked examples](#6-worked-examples)
7. [Notes on accuracy](#7-notes-on-accuracy)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Building

Requires GCC with C++20 (GCC 12 or newer) and GNU make. Nothing else —
the Z80 core and the DEFLATE compressor are vendored in `lib/`.

```sh
make                # release build, statically linked → bin/bin/zx-spectrum-mcp
make test           # build and run the test suite with sanitizers
make debug          # sanitizer build → bin/bin/zx-spectrum-mcp-debug
make package        # stage copyable bin/ and share/ directories under bin/
make list-tools     # print every tool schema as JSON
make help           # all targets
```

The release binary is fully static:

```sh
$ ldd bin/bin/zx-spectrum-mcp
	not a dynamic executable
```

so it can be copied to another machine as it stands.

### Installing on Unix

The package is a flat filesystem overlay:

```sh
make package
sudo cp -a bin/bin bin/share /usr/local/
```

It contains the static executable in `bin/`, ROMs in the read-only
`share/zx-spectrum-mcp/roms/` data directory, and documentation under
`share/doc/zx-spectrum-mcp/`. Build a self-contained `/opt` tree with:

```sh
make package
sudo mkdir -p /opt/zx-spectrum-mcp
sudo cp -a bin/bin bin/share /opt/zx-spectrum-mcp/
```

`sudo make install` performs the direct equivalent; `DESTDIR` is supported
for packaging systems. `/var` is deliberately not used because the bundled
ROMs and documentation are immutable. Screenshots and videos are written only
to paths explicitly supplied to their tools.

---

## 2. Running it

```sh
bin/bin/zx-spectrum-mcp [options]
```

| Option | Meaning |
|---|---|
| `--rom PATH` | Load a 16K ROM image before serving. |
| `--interface1-rom PATH` | Load an 8K Interface 1 shadow ROM and attach its serial ports. |
| `--load PATH` | Load a program, snapshot, or TAP/TZX tape at startup; format taken from the extension. |
| `--serial` | Enable Interface 1 RS-232 on TCP data/control ports 6601/6602. |
| `--serial-data-port PORT` | Change the raw data listener and enable serial. |
| `--serial-control-port PORT` | Change the control listener and enable serial. |
| `--verbose` | Log protocol activity to **stderr**. |
| `--list-tools` | Print the tool schemas as JSON and exit. |
| `--version` | Print the version and exit. |
| `--help` | Print usage and exit. |

With no options it serves the protocol on stdin/stdout and waits.

### About the ROM

The locally provided images are in `data/roms/`: `48.rom` is the 16K
Spectrum ROM and `if1-2.rom` is the 8K Interface 1 shadow ROM. Their
copyright is separate from the GPL-covered emulator source, and `*.rom`
remains gitignored, so verify your right to redistribute the binary images.
Without `--rom`, the machine boots a small built-in stub that is enough to
load and run machine code.

Supply a real 48K ROM to get BASIC, the ROM keyboard scan, and character
recognition in `screen_text`:

```sh
bin/bin/zx-spectrum-mcp --rom data/roms/48.rom
```

### Interface 1 serial bridge

Enable the serial portion of Sinclair's ZX Interface 1 with:

```sh
bin/bin/zx-spectrum-mcp --rom data/roms/48.rom \
                    --interface1-rom data/roms/if1-2.rom \
                    --serial
```

This opens two IPv4 listeners on all local interfaces. They are deliberately
disabled unless requested, because the protocol has no authentication.

| Default | Connection | Protocol |
|---:|---|---|
| 6601 | data | Raw eight-bit bytes, full duplex; no Telnet negotiation or escaping. |
| 6602 | control | Case-insensitive, newline-terminated text commands and notifications. |

Choose different ports when registering the MCP server if necessary:

```sh
bin/bin/zx-spectrum-mcp --serial-data-port 7001 \
                    --serial-control-port 7002
```

Only one data client and one control client are active at a time. A client
may disconnect and reconnect without restarting the emulator. Input and
output queues are bounded, and every socket operation is non-blocking.

The control client may send:

| Command | Reply | Effect |
|---|---|---|
| `PING` | `PONG` | Check that the control connection is alive. |
| `CTS 0` or `CTS 1` | `OK CTS` | Override the peer clear-to-send input. `ON`, `OFF`, `TRUE`, `FALSE`, `YES`, and `NO` also work. |
| `CTS AUTO` | `OK CTS AUTO` | Make CTS follow the data-client connection again. |
| `DCD 0` or `DCD 1` | `OK DCD` | Override carrier detect. |
| `DCD AUTO` | `OK DCD AUTO` | Make DCD follow the data-client connection again. |

Bad arguments receive `ERR CTS <0|1|AUTO>` or
`ERR DCD <0|1|AUTO>`; an unknown command receives `ERR UNKNOWN`.
Replies end in LF. An optional CR before an incoming LF is ignored.

The bridge publishes these lines whenever the emulated outputs change:

```text
RTS 1
DTR 1
```

Interface 1 labels its connector as DCE rather than DTE, so its hardware
names do not line up neatly with the generic TCP protocol. The mapping is:

| TCP control signal | Interface 1 behavior |
|---|---|
| incoming `CTS` | Drives status-port `$EF` bit 3, the Interface 1 `DTR` input. Both mean that the peer permits the Spectrum to transmit. |
| incoming `DCD` | Retained and reported by `status`; Interface 1 has no separate carrier-detect pin. |
| outgoing `RTS` | Follows control-port `$EF` bit 4, called `CTS` by Interface 1. It gates data-socket input by default. |
| outgoing `DTR` | High while `$EF` bit 0 selects RS-232 rather than Sinclair Network output. |

The data registers themselves match the hardware: addresses whose low decode
bits select `$EF` expose control/status; those selecting `$F7` expose the
inverted receive/transmit bit. The Interface 1 ROM's start/data/stop access
sequence is decoded into raw TCP bytes, so its supported baud choices do not
need a wall-clock baud setting on the host connection.

This is the RS-232 hardware subset, not the full Interface 1: Microdrives
and Sinclair Network are not emulated. With `--interface1-rom`, the 8K image
pages at opcode-fetch traps `$0008` and `$1708`, mirrors through the lower
16K, and unpages after the opcode fetch at `$0700`. This gives the extended
BASIC `FORMAT`, `OPEN #`, `LPRINT`, and `INKEY$ #` paths their real ROM
environment while serial I/O reaches the TCP bridge.

### stdout is the protocol

The server writes **only** JSON-RPC messages to stdout. Every
diagnostic goes to stderr. If you wrap the server in a script, do not
let anything else print to stdout or the session will break.

---

## 3. Connecting a client

Any MCP client that speaks the stdio transport works. For Claude Code:

```sh
claude mcp add spectrum -- /absolute/path/to/bin/bin/zx-spectrum-mcp --rom /path/to/48.rom
```

Or in a client config file:

```json
{
  "mcpServers": {
    "spectrum": {
      "command": "/absolute/path/to/bin/bin/zx-spectrum-mcp",
      "args": ["--rom", "/path/to/48.rom"]
    }
  }
}
```

You can also drive it by hand, which is the quickest way to see it work:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  | bin/bin/zx-spectrum-mcp
```

---

## 4. The protocol in five minutes

Standard MCP over newline-delimited JSON-RPC 2.0. One message per line.

**1 — Client initialises.** The server answers with the protocol
revision it will use, what it can do, and who it is.

```json
{"jsonrpc":"2.0","id":1,"method":"initialize",
 "params":{"protocolVersion":"2025-06-18","capabilities":{},
           "clientInfo":{"name":"my-client","version":"1.0"}}}
```

```json
{"jsonrpc":"2.0","id":1,"result":{
  "protocolVersion":"2025-06-18",
  "capabilities":{"tools":{"listChanged":false}},
  "serverInfo":{"name":"zx-spectrum-mcp","version":"1.0.0"}}}
```

Revisions `2025-06-18`, `2025-03-26` and `2024-11-05` are accepted; ask
for anything else and the server answers with its own.

**2 — Client confirms.** A notification, so there is **no reply**.

```json
{"jsonrpc":"2.0","method":"notifications/initialized"}
```

**3 — Client asks what there is.**

```json
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
```

**4 — Client calls a tool.**

```json
{"jsonrpc":"2.0","id":3,"method":"tools/call",
 "params":{"name":"run","arguments":{"frames":1}}}
```

`ping` is also supported and answers with an empty result.

### Results

Every tool returns `content` blocks, and most also return
`structuredContent` — the same information as JSON, so you can parse
instead of scraping the text.

```json
{"jsonrpc":"2.0","id":3,"result":{
  "content":[{"type":"text","text":"completed after 69888 T-states …"}],
  "structuredContent":{"reason":"completed","tstates":69888,"pc":32783}}}
```

### Two kinds of failure

This distinction matters when writing a client:

- **The tool could not do what you asked** — result comes back with
  `"isError": true` and text explaining why. The call succeeded at the
  protocol level; read the message and try something else.
- **The call itself was malformed** — unknown tool name, missing
  `name`, bad JSON — comes back as a JSON-RPC `error` object.

### Numbers accept hex

Anywhere a tool takes an address, port or byte, you may send a JSON
number *or* a string in hex: `"0x5C00"`, `"$5C00"` and `"#5C00"` all
work, as does plain `"23552"`.

---

## 5. Tool reference

Nineteen tools. Required arguments are marked **required**.

### Getting code in

#### `load`

Load a program or machine state.

| Argument | Type | Meaning |
|---|---|---|
| `path` | string | File to read from disk. |
| `data` | string | Inline bytes as hex pairs, e.g. `"3e02d3fe"`. |
| `format` | enum | `binary`, `scr`, `sna`, `z80`, `tap`, `tzx`, `rom`. Guessed from `path` if omitted. |
| `address` | 0–65535 | Where binary data goes. Default `32768` (`0x8000`). |
| `start` | 0–65535 | Set the program counter here after a binary load. |
| `reset` | boolean | Reset and clear RAM first. Snapshots always reset. |
| `autoplay` | boolean | Start TAP/TZX immediately. Default `true`; ignored for other formats. |

Supply **exactly one** of `path` or `data`.

| Format | What it is |
|---|---|
| `binary` | Raw bytes at `address`. What an assembler produces. |
| `scr` | 6912-byte screen dump. Loads a picture; does not disturb the CPU. |
| `sna` | 48K snapshot. The program counter is popped off the stack, as the format intends. |
| `z80` | Snapshot versions 1, 2 and 3, including the run-length encoding. 48K only; a 128K image is refused with a clear message. |
| `tap` | Standard Spectrum tape blocks, expanded to the exact ROM pilot, sync, bit and pause timings. |
| `tzx` | Timing-preserving cassette image for ROM, turbo and custom loaders. |
| `rom` | Replaces the lower 16K and resets. |

Tape playback is waveform-level emulation, not a shortcut that copies bytes
into RAM. It advances only when the machine runs and feeds the current signal
to bit 6 of ULA port `$FE`. TZX supports standard and turbo data, pure tone,
pulse sequence, pure data, direct recording, RLE/Z-RLE CSW, generalized data,
pause/stop, signal level, loops, jumps, calls, selections, groups and metadata.
Deprecated C64 blocks and embedded snapshots are rejected explicitly.

#### `tape`

| Argument | Type | Meaning |
|---|---|---|
| `action` | enum | `status` (default), `play`, `stop`, `rewind`, or `eject`. |

The structured result includes format, title when present, block and segment
counts, EAR level, total duration and current position in T-states, and whether
a TZX command stopped playback. `play` resumes after such a stop. A machine
reset preserves the inserted image but rewinds and stops it.

For a loader which must be started before the signal arrives, insert without
autoplay, start the loader, then start the deck:

```json
{"name":"load","arguments":{"path":"loader-test.tzx","autoplay":false}}
{"name":"run_until","arguments":{"address":"0x8123"}}
{"name":"tape","arguments":{"action":"play"}}
{"name":"run","arguments":{"frames":500}}
```

#### `reset`

| Argument | Type | Meaning |
|---|---|---|
| `clear_memory` | boolean | Also wipe all 48K of RAM. Default `false`, which is what real hardware does. |

### Running

#### `run`

Run until a limit is reached or a breakpoint fires.

| Argument | Type | Meaning |
|---|---|---|
| `frames` | 1–1000 | Video frames. One frame is 69888 T-states. |
| `tstates` | 1–70000000 | CPU T-states. |
| `instructions` | 1–100000000 | Instructions. |
| `stop_on_halt` | boolean | Stop as soon as the CPU executes `HALT`. |

Give at most one limit; with none, one frame runs. Every call is capped
at about twenty seconds of emulated time so a wedged program cannot hang
the server.

Returns `reason`, one of `completed`, `breakpoint`, `halted`,
`address_reached`, `step_limit`.

#### `run_until`

| Argument | Type | Meaning |
|---|---|---|
| `address` | 0–65535 | **required.** Stop when `pc` reaches this. |
| `max_tstates` | 1–70000000 | Give up after this long. Default 3500000, one second. |

Tested between instructions, so the address must be an instruction
boundary. `reason` is `address_reached` on success, `completed` if the
limit was hit first.

#### `step`

| Argument | Type | Meaning |
|---|---|---|
| `count` | 1–1000000 | Instructions to execute. Default 1. |

#### `status`

No arguments. Reports clock speed, frame geometry, contention model,
whether a ROM is loaded, border colour, breakpoint count and elapsed
time and the current cassette state. With Interface 1 attached, it also
reports shadow-ROM state. With `--serial`, it returns the data/control
ports, client connections, modem
lines, byte counters, and pending queue sizes in the structured `serial`
object.

### Memory

#### `read_memory`

| Argument | Type | Meaning |
|---|---|---|
| `address` | 0–65535 | **required.** First address. |
| `length` | 1–4096 | Bytes to read. Default 16. |

Returns an annotated hex dump plus a `bytes` array. No side effects, no
emulated time.

Useful landmarks: `0x4000` display file, `0x5800` attributes, `0x5B00`
printer buffer, `0x5C00` system variables.

#### `write_memory`

| Argument | Type | Meaning |
|---|---|---|
| `address` | 0–65535 | **required.** First address. |
| `data` | string/array | **required.** Hex pairs or an array of byte values. |
| `allow_rom` | boolean | Permit writes below `0x4000`. Default `false`. |

Reports how many bytes were actually written; anything that fell in ROM
is skipped unless `allow_rom` is set.

### CPU

#### `registers`

With no arguments, reads the register file. Any register named is
**written first**, so this both inspects and pokes.

Accepts `af`, `bc`, `de`, `hl`, `ix`, `iy`, `sp`, `pc`, `af_alt`,
`bc_alt`, `de_alt`, `hl_alt` (0–65535), `i`, `r` (0–255), `im` (0–2),
`iff1`, `iff2` (boolean).

`pc` is the address of the instruction **about to execute**. Flags come
back as letters in the order `SZYHXPNC`, upper case when set.

#### `breakpoint`

| Argument | Type | Meaning |
|---|---|---|
| `action` | enum | `add`, `remove`, `list`, `clear`, `enable`, `disable`. Default `list`. |
| `kind` | enum | `execute`, `memory_read`, `memory_write`, `io_read`, `io_write`. |
| `address` | 0–65535 | Address or port to watch. |
| `value` | 0–255 | Only fire when the byte transferred equals this. |
| `id` | integer | Which breakpoint to act on. |

`add` returns an id. Execute breakpoints stop **before** the instruction
runs, leaving `pc` on it; the others stop at the end of the instruction
that made the access, so the machine is never left mid-opcode.

Resuming from an address that has a breakpoint on it works — the
breakpoint at the position you start from is not re-triggered.

### Keyboard and ports

#### `press_keys`

| Argument | Type | Meaning |
|---|---|---|
| `text` | string | Text to type, one character at a time. |
| `keys` | string[] | Key names held together as one chord. |
| `hold_frames` | 1–100 | Frames each keystroke is held. Default 3. |
| `gap_frames` | 0–100 | Frames released between keystrokes. Default 2. |

Supply **exactly one** of `text` or `keys`. **This advances emulated
time** — it has to, because the ROM samples the keyboard once per frame
from the interrupt routine and debounces it, so a key that goes down and
up with no frames in between is never seen.

With `text`, shifts are worked out for you: upper case adds caps shift,
punctuation adds symbol shift, `\n` is ENTER.

Key names: `A`–`Z`, `0`–`9`, `ENTER`, `SPACE`, `CAPS_SHIFT`,
`SYMBOL_SHIFT`. Matching ignores case, spaces, hyphens and underscores,
and `CS`, `SS`, `RETURN` and `BREAK` are accepted as aliases.

#### `read_port`

| Argument | Type | Meaning |
|---|---|---|
| `port` | 0–65535 | **required.** Full 16-bit port address. |

Port decoding is **partial**, so the whole 16-bit address matters:
`0xFEFE` reads the caps-shift half-row, `0x7FFE` the space half-row. A
port no device claims returns the ULA floating bus. No emulated time
passes and no contention is applied.

#### `set_port`

| Argument | Type | Meaning |
|---|---|---|
| `port` | 0–65535 | **required.** |
| `value` | 0–255 | **required.** |

Writing an even port reaches the ULA: bits 0–2 set the border, bit 3 is
tape output, bit 4 the speaker.

### Screen

#### `screen`

| Argument | Type | Meaning |
|---|---|---|
| `include_border` | boolean | Include the border. Default `true` → 352×288. Without it, 256×192. |
| `scale` | 1–4 | Integer magnification. Default 1. |

Returns a PNG image block plus a text summary. The picture is whatever
the beam has painted, so mid-frame effects appear exactly as they would
on a television.

#### `screen_text`

| Argument | Type | Meaning |
|---|---|---|
| `mode` | enum | `chars` or `ascii`. Default `chars`. |
| `font_address` | 0–65535 | Bitmap address of character 32, overriding `CHARS`. |
| `columns` | 8–200 | ASCII art width. Default 64. |
| `rows` | 4–100 | ASCII art height. Default 24. |
| `trim` | boolean | Strip trailing spaces. Default `true`. |

**`chars`** matches each 8×8 cell against the character set the machine
is actually using, found through the `CHARS` system variable — so a
program that redefines the font is still read correctly. Inverse video
is recognised; unmatched cells become `?`. Check `font_usable` in the
result: without a ROM there is no font to match against.

**`ascii`** renders the picture as ASCII art by brightness. Use it for
graphics, and when there is no font.

For reading text off the screen this is far cheaper than `screen` — a
grid of characters instead of a base64 image.

#### `screenshot`

| Argument | Type | Meaning |
|---|---|---|
| `path` | string | **required.** Destination PNG file. An existing file is replaced. |
| `include_border` | boolean | Include the border. Default `true` → 352×288. Without it, 256×192. |
| `scale` | 1–4 | Integer magnification. Default 1. |

Writes the current beam-rendered screen to disk and returns the path,
dimensions, frame number, and file size. The parent directory must already
exist. Unlike `screen`, it does not put the image bytes into the MCP reply.

#### `video_start`

| Argument | Type | Meaning |
|---|---|---|
| `path` | string | **required.** Destination `.y4m` file. An existing file is replaced. |
| `include_border` | boolean | Record 352×288 with the border (default), or crop to 256×192. |

Starts a YUV4MPEG2 C444 stream. Each complete frame produced by `run`,
`step`, or `press_keys` is appended at the exact rate
`3500000/69888` Hz (about 50.08 Hz). Pausing the emulator adds no frames,
so recording is deterministic and independent of host speed. Only one
recording can be active.

YUV4MPEG2 is uncompressed and needs no codec in the MCP server. The
tradeoff is file size: border-inclusive video is about 15 MB per second.
Players such as `ffplay` can open it directly, or it can be converted to
a compressed format afterwards.

#### `video_stop`

No arguments. Flushes and closes the active video and returns its path,
dimensions, frame count, exact emulated duration, and byte size. Always call
this before moving or converting the file.

---

## 6. Worked examples

### Load and run machine code

This program sets the border red, fills the screen, and halts.

```
        ld   a,2
        out  (0xfe),a       ; border red
        ld   hl,0x4000
        ld   de,0x4001
        ld   bc,0x17ff
        ld   (hl),0xff      ; every pixel on
        ldir
        ld   hl,0x5800
        ld   de,0x5801
        ld   bc,0x02ff
        ld   (hl),0x47      ; bright white on black
        ldir
loop:   halt
        jr   loop
```

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{
  "name":"load","arguments":{
    "data":"3e02d3fe21004011014001ff1736ffedb021005811015801ff023647edb07618fd",
    "address":"0x8000","start":"0x8000"}}}
```

```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{
  "name":"run","arguments":{"frames":6}}}
```

Then `screen_text` with `mode: "ascii"`:

```
::::::::::::::::::::::::::::::::::::::::
:::::=****************************=:::::
:::::+@@@@@@@@@@@@@@@@@@@@@@@@@@@@*:::::
:::::+@@@@@@@@@@@@@@@@@@@@@@@@@@@@*:::::
:::::=****************************=:::::
::::::::::::::::::::::::::::::::::::::::
```

Six frames, not two. The two `LDIR`s take about 145000 T-states, so the
attribute fill does not finish until part way through the third frame —
and until it does, the screen really is black ink on black paper. That
is the emulator being right, not slow.

### Save a screenshot or record video

```json
{"name":"screenshot","arguments":{"path":"capture.png","scale":2}}
{"name":"video_start","arguments":{"path":"capture.y4m"}}
{"name":"run","arguments":{"frames":250}}
{"name":"video_stop","arguments":{}}
```

That recording contains exactly 250 frames, or about 4.99 seconds of
Spectrum time, regardless of how quickly the host executed the `run` call.

### Type a BASIC program

With a real ROM loaded:

```json
{"name":"press_keys","arguments":{"text":"PRINT \"HELLO\"\n"}}
```

then read it back:

```json
{"name":"screen_text","arguments":{"mode":"chars"}}
```

### Debug with breakpoints

```json
{"name":"breakpoint","arguments":{"action":"add","kind":"execute","address":"0x8010"}}
{"name":"run","arguments":{"frames":10}}
{"name":"registers","arguments":{}}
```

The run stops with `reason: "breakpoint"` and `pc` on `0x8010`.

Watch for a write instead:

```json
{"name":"breakpoint","arguments":{"action":"add","kind":"memory_write","address":"0x5800","value":71}}
```

### Border raster stripes

The classic effect, and the clearest demonstration that the emulator is
cycle accurate. This loop writes a new border colour every 166 T-states,
faster than a scan line takes:

```
loop:   out  (0xfe),a       ; 11T
        inc  a              ;  4T
        and  7              ;  7T
        ld   b,10           ;  7T
wait:   djnz wait           ; 13T per pass
        jr   loop           ; 12T
```

```json
{"name":"load","arguments":{"data":"d3fe3ce607060a10fe18f5","address":"0x8000","start":"0x8000"}}
{"name":"run","arguments":{"frames":2}}
{"name":"screen","arguments":{}}
```

The border comes back as horizontal stripes. Load the identical program
at `0x5B00` instead — contended memory — and the stripes land in
different places, because the ULA is now stealing cycles from the CPU.

---

## 7. Notes on accuracy

- **Frame**: 69888 T-states, 224 per line × 312 lines, 3.5 MHz.
- **Interrupt**: asserted at T-state 0 for 32 T-states.
- **First display pixel**: drawn at T-state 14336. Contention starts at
  14335, one T-state earlier, because the ULA fetches ahead of the beam.
- **Contention**: `6,5,4,3,2,1,0,0` across each 8 T-state block of a
  display line, for addresses `0x4000`–`0x7FFF`. I/O contention follows
  the standard four-case rule.
- **Rendering**: the ULA is ticked once per T-state and paints the two
  pixels the beam covers. Nothing is deferred to the end of the frame.
- **The instruction at `pc`** has not executed yet. `run` and `step`
  report `pc` as the address of the next instruction, not the raw
  internal value.

Verified by `make test` — 509 C++ checks plus 1,356 pinned Fuse Z80 vectors,
including 25 published instruction timings, Interface 1 serial and shadow
ROM paging, capture files, and contention applied at chosen points in the
frame. See
[`docs/notes/ARCHITECTURE.md`](../notes/ARCHITECTURE.md) for the
measurements behind the timing model, and for what is deliberately not
implemented.

---

## 8. Troubleshooting

**`screen_text` returns nothing but `?`** — there is no font to match
against. Check `font_usable` in the result. Load a ROM with `--rom`, or
pass `font_address`, or use `mode: "ascii"`.

**Nothing happens when I type** — `press_keys` needs the ROM's keyboard
scan, which needs a ROM. Without one, poke memory directly instead.
Also check that interrupts are enabled: the scan runs from the frame
interrupt handler.

**The screen is blank after loading a program** — run more frames. Work
takes emulated time, and the beam paints what is there when it passes.
The `LDIR` example above needs six frames, not one.

**`run_until` returns `completed` instead of `address_reached`** — the
address was never reached inside the limit, or it is not an instruction
boundary. The check happens between instructions.

**A `.z80` snapshot is refused** — it is probably a 128K image. Only 48K
snapshots can be restored; the error message says which hardware mode it
found.

**The session dies after one message** — something is writing to stdout
besides the server. Only JSON-RPC may go there; send diagnostics to
stderr.

**Timings look wrong by a T-state or two** — measure between instruction
boundaries. The CPU core overlaps the opcode fetch of the next
instruction with the tail of the current one, so a measurement that
starts mid-instruction reads high.
