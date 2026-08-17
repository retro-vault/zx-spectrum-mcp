# Vendored Z80 CPU core

This directory contains a third-party Z80 emulator. It is **not** covered
by the project coding standard and must be kept byte-identical to
upstream so that it can be re-synchronised cleanly.

## Source

| Field      | Value                                                |
|------------|------------------------------------------------------|
| Project    | `chips` by Andre Weissflog (floooh)                  |
| Repository | https://github.com/floooh/chips                      |
| File       | `chips/z80.h`                                        |
| Commit     | `ca7d7ddd3ba77b48685d24120cf413ea53786767`            |
| Licence    | zlib/libpng (see `LICENSE.upstream`)                 |
| Vendored   | 2026-08-17                                           |

The zlib licence is GPL-3.0 compatible, so it may be combined with the
GPL-3.0 code in the rest of this project.

## Why this core

The server has to be cycle exact, including ULA memory contention. That
rules out most Z80 cores, which are instruction stepped: they execute a
whole instruction and then report how many T-states it took. Contention
cannot be modelled faithfully after the fact, because the ULA stalls the
CPU *in the middle* of a machine cycle.

`z80.h` is **tick stepped**. One call to `z80_tick()` advances the CPU by
exactly one T-state and exchanges a 64-bit pin mask with the caller,
carrying the address bus, data bus and control signals (`M1`, `MREQ`,
`IORQ`, `RD`, `WR`, `RFSH`, `WAIT`, ...).

That buys two things this project cannot do without:

1. The ULA can be clocked in lockstep with the CPU, one T-state at a
   time, so the beam and the processor genuinely interleave.
2. Every machine cycle announces itself on the bus, so contention can be
   applied to the cycle that actually caused it.

### Note on the `WAIT` pin

The core does implement `WAIT` faithfully:

```c
#define _wait()  {if(pins&Z80_WAIT)goto step_to;}
```

There are 297 such points, covering every memory and I/O cycle. The
obvious design would be to drive contention through that pin.

**It does not work, and the emulator deliberately does not use it.**
For every machine cycle except the opcode fetch, the core evaluates
`_wait()` *before* it puts the address on the bus, so the decision has to
be made a T-state before the information needed to make it exists.
`tests/test_cpu_timing.cpp` demonstrates this: injecting one wait state
on the rising edge of `MREQ` lengthens `NOP` by 1 T-state as intended,
but leaves `LD BC,nn` short, because only its opcode fetch honours the
pin.

Contention is therefore applied by stalling the clock instead — `cpu::tick()`
returns the number of T-states the bus imposed, and `machine` advances the
ULA that many T-states without ticking the CPU. That is what `WAIT` does
physically, and it puts the decision where the address is known. The pin
remains available for a future device that can decide a stall in advance.

Alternatives considered:

- **redcode/Z80** — extremely accurate and well documented, but executes
  in instruction-sized chunks. Per-T-state contention and beam
  synchronous rendering would have to be bolted on top.
- **z80ex / libz80** — instruction stepped, same objection.

`chips/z80.h` also passes the standard `zexall` / `zexdoc` exercisers and
models the undocumented flags (`YF`/`XF`) and most `MEMPTR`/`WZ`
behaviour. The adapter in `lib/spectrum/cpu.cpp` supplies the NMOS `Q`
latch behaviour and corrects `MEMPTR` after repeating block-I/O
instructions; these are kept outside this file so the vendored source
stays byte-identical to upstream. The pinned Fuse vectors in
`tests/data/fuse-z80/` cover both corrections.

## Local modifications

None. The file is byte-identical to upstream.

If a change ever becomes unavoidable, record it here and mark it clearly
in the source, as required by clause 2 of the zlib licence.

## Re-syncing

```sh
git clone --depth 1 https://github.com/floooh/chips.git
cp chips/chips/z80.h lib/z80/include/z80/z80.h
```

Then re-run `make test`. The exerciser tests in `tests/` will catch a
regression in the core itself, and `tests/test_contention.cpp` will catch
a change in how the `WAIT` pin is interpreted.
