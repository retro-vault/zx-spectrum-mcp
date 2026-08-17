# Fuse Z80 core test vectors

The files `tests.in`, `tests.expected`, and `FORMAT` are copied verbatim
from the Fuse ZX Spectrum emulator's Z80 core test suite.

| Field | Value |
|---|---|
| Project | Fuse, the Free Unix Spectrum Emulator |
| Repository | https://github.com/fuse-emulator/fuse |
| Path | `z80/tests/` |
| Commit | `df875d6a4b7bc0fba7b68f480ac081d7693ad329` |
| Retrieved | 2026-08-17 |
| Licence | GPL-2.0-or-later (see `COPYING`) |
| `tests.in` SHA-256 | `9f36e866f22e72ff1f8bf2100bf70ffbf58edd97b453500aab60acf1f403ebbb` |
| `tests.expected` SHA-256 | `15a6946f4addcf97e137b5bdd1d5fdb08124ff91f1b169f36a8bf4afe4bab6e4` |

There are 1,356 independent vectors. They cover the base, CB, ED,
DD/FD, and DDCB/FDCB opcode spaces, including flags, alternate and
index registers, MEMPTR/WZ, memory effects, I/O, HALT, and instruction
timing. `test_z80_vectors.cpp` runs them against the public
`spectrum::cpu` and `bus_interface` APIs.

The GPL-2.0-or-later terms are compatible with this project's GPL-3.0
licence. The upstream licence text is included unchanged in `COPYING`.
