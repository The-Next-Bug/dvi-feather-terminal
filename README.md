# dvi-feather-terminal

A configurable VT100-style serial terminal with DVI output, for the
[Adafruit Feather RP2040 DVI](https://www.adafruit.com/product/5710): UART
in, DVI out (640x480, 80x30 cells of 8x16), USB keyboard in, UART out. It is
meant to become the console for later RP2040/RP2350 "compute" boards
(e.g. RunCPM).

Video uses [PicoDVI](https://github.com/Wren6991/PicoDVI)'s `libdvi` and its
per-character colour text encoder: core 1 renders each scanline straight from
the character cell buffer (no framebuffer), core 0 runs everything else.

Status: milestone 2 — a "dumb" teletype. UART0 (115200 8N1, TX = GP0,
RX = GP1) is received by interrupt into a ring buffer and shown in rows 0-28
with CR, LF, BS, TAB, deferred autowrap, scrolling and a blinking block
cursor; row 29 is a status line with receive/overrun/drop/error counts.
Nothing is transmitted yet (no keyboard). Work is tracked with `bd`
(beads): `bd list`.

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware. Portable and host-tested: `screen.[ch]` (cell buffer), `term.[ch]` (teletype). RP2040-specific: `video.[ch]` (libdvi renderer on core 1), `uart_rx.[ch]` (IRQ receive). `font_gen.py` builds the font table |
| `test/` | Host unit tests (plain CMake + gcc, no pico-sdk) |
| `lib/pico-sdk`, `lib/PicoDVI` | Pinned submodules |
| `assets/fonts/` | Terminus Font 8x16 (PSF) and its licence (SIL OFL 1.1) |
| `cmake/build_info.cmake` | Generates `term_build.h` (git describe, commit, date) on every build |
| `scripts/` | `setup-toolchain.sh`, `build.sh`, `test.sh`, `package.sh`, `changelog.sh` |
| `toolchain.env` | Pinned CMake, Arm GNU Toolchain and picotool versions |
| `docs/FLASHING.txt` | End-user flashing steps, shipped in the package |

## Font and glyph codes

The screen buffer stores one-byte glyph codes (see `src/font.h`): ASCII at
0x20-0x7E, Latin-1 at 0xA0-0xFF, DEC Special Graphics (line drawing) at
0x01-0x1F, and shades/blocks/double-line box drawing at 0x80-0x9F. The table
is generated at build time from `assets/fonts/Uni2-Terminus16.psf.gz`; glyphs
the console font lacks (control pictures, scan lines, half blocks) are drawn
by `font_gen.py`.

## Building locally

Everything installs under `.toolchain/` in the repo (git-ignored); only
`curl`, `git`, `python3`, `xz` and a host C/C++ compiler with `make` are
needed.

```sh
git submodule update --init          # pico-sdk, PicoDVI (not their submodules)
scripts/setup-toolchain.sh           # CMake, Arm GNU Toolchain, picotool
scripts/test.sh                      # host unit tests
scripts/build.sh                     # -> build/feather-rp2040-dvi/dvi_terminal.uf2, build-info.txt
scripts/package.sh                   # -> dist/dvi-terminal-<rev>.zip, dist/CHANGES.md
```

`scripts/build.sh` takes `TARGET` (default `feather-rp2040-dvi`), which
selects the pico-sdk board and PicoDVI pin configuration. Plain `cmake -S .
-B build/x` also works and picks up `.toolchain/` automatically.

Toolchain versions are pinned in **`toolchain.env`**, the single source of
truth for `scripts/setup-toolchain.sh` and the CI cache key. To upgrade,
change the version and its SHA256 there. pico-sdk and PicoDVI are upgraded
by moving their submodules.

## CI

- `test` — host unit tests (JUnit report).
- `build` — firmware per target (matrix); UF2/BIN/ELF/disassembly/map and
  `build-info.txt` artifacts.
- `package` — ZIP with UF2 and BIN per target, `FLASHING.txt`, `CHANGES.md`
  (commits since the previous tag), `SHA256SUMS` and the source of the built
  commit. Automatic on the default branch and tags.
- `release` — tags only: uploads the ZIP to the project's generic package
  registry (`dvi-terminal/<tag>`) and creates a GitLab Release linking to it,
  with `CHANGES.md` as the description.

## Releasing

```sh
git tag -a v0.1.0 -m "dvi-feather-terminal 0.1.0"
git push origin v0.1.0
```

## Licences

Terminus Font is © Dimitar Toshkov Zhekov under the SIL Open Font License
1.1; see `assets/fonts/Terminus-OFL.txt`. The generated font table is a
derivative and stays under that licence. pico-sdk and PicoDVI are
BSD-3-Clause.
