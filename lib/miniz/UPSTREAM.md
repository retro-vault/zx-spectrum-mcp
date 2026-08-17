# Vendored DEFLATE compressor (miniz)

Third-party code. Not covered by the project coding standard; keep it in
step with upstream rather than reformatting it.

## Source

| Field      | Value                                          |
|------------|------------------------------------------------|
| Project    | `miniz` by Rich Geldreich et al.               |
| Repository | https://github.com/richgel999/miniz            |
| Commit     | `77d0dce8627735138c51770d1799a1ef48f2117d`      |
| Licence    | MIT (see `LICENSE.upstream`)                   |
| Vendored   | 2026-08-17                                     |

MIT is GPL-3.0 compatible.

## Why it is here

The `screen` tool returns a PNG, and PNG image data is a zlib stream, so
something has to DEFLATE it. An uncompressed screenshot is roughly 50 KB
before base64, which then has to travel through an MCP response and sit
in a model's context window; compressed it is typically one to three KB.

The system `libz.a` would have done the job, but the project links
statically and must build on a machine without `zlib1g-dev` installed.
Vendoring keeps the tree self-contained, the same way `lib/z80` is.

Only three entry points are used, all from `miniz.c`:

- `mz_compressBound()` — output buffer sizing
- `mz_compress2()` — produces the zlib stream for the `IDAT` chunk
- `mz_crc32()` — the per-chunk CRC that PNG requires

## Build configuration

Compiled with two macros, set in `lib/miniz/Makefile`:

- `MINIZ_NO_ARCHIVE_APIS` — drops the ZIP reader and writer, which this
  project has no use for.
- `MINIZ_NO_STDIO` — drops the file I/O helpers, so the library never
  touches the filesystem behind our back.

`miniz_zip.c` is therefore not compiled at all. Its header is still
vendored because `miniz.h` includes it unconditionally; with the macro
set, the header expands to nothing.

## Local modifications

One added file, `include/miniz/miniz_export.h`.

Upstream generates it with CMake to control shared-library symbol
visibility. This project does not use CMake and links miniz statically
into one executable, so the shim defines both export macros as empty. It
is marked as a local addition in its own header comment.

No upstream file has been edited.

## Re-syncing

```sh
git clone --depth 1 https://github.com/richgel999/miniz.git
cp miniz/miniz.c miniz/miniz_tdef.c miniz/miniz_tinfl.c lib/miniz/
cp miniz/miniz.h miniz/miniz_common.h miniz/miniz_tdef.h \
   miniz/miniz_tinfl.h miniz/miniz_zip.h lib/miniz/include/miniz/
```

Keep `miniz_export.h`. Then run `make test`; `tests/test_png.cpp`
decodes what the encoder produces and will catch a breaking change.
