# dvi-feather-terminal

A configurable VT100-style serial terminal with DVI output, for the
[Adafruit Feather RP2040 DVI](https://www.adafruit.com/product/5710): UART
in, DVI out (800x600p60, 80 or 132 columns by 24 rows plus a status line),
USB keyboard in, UART out. It is
meant to become the console for later RP2040/RP2350 "compute" boards
(e.g. RunCPM).

Video uses [PicoDVI](https://github.com/Wren6991/PicoDVI)'s `libdvi` and its
2bpp text encoder at 800x600p60 (400 MHz, 1.3 V). Core 0 rasterises changed
screen lines into per-line pixel buffers (`src/raster.c`): 8-pixel cells
with Terminus 8x16 for 80 columns, 6-pixel cells with misc-fixed 6x13 for
132, each text row 24 scanlines. Line-drawing, block and shade glyphs extend
into the space between rows, so vertical lines join up. Core 1 encodes the
buffers to TMDS one scanline at a time and draws the blinking cursor.

The encoder has two colour levels per 8-pixel group and colour plane, so in
132 columns (where cells straddle groups) colours are exact as long as the
cells sharing a group show at most two levels per plane — true for normal
text on one background and for reverse video. 80 columns is always exact.

Status: milestone 2 — a "dumb" teletype on UART0 (115200 8N1, TX = GP0,
RX = GP1): interrupt-driven receive into a ring buffer, CR, LF, BS, TAB,
deferred autowrap, scrolling and a blinking block cursor in rows 0-23; row
24 is a status line with receive/overrun/drop/error counts. Nothing is
transmitted yet (no keyboard).

At startup a demo alternates between 80 and 132 columns every 5 seconds
until the first byte arrives on UART0, which starts the terminal (build
with `-DSTARTUP_DEMO=OFF` to skip it). Work is tracked with `bd` (beads):
`bd list`.

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware. Portable and host-tested: `screen.[ch]` (cell buffer), `term.[ch]` (teletype), `raster.[ch]` (cells to pixels), `demo.[ch]`. RP2040-specific: `video.[ch]` (libdvi, core 1), `uart_rx.[ch]` (IRQ receive). `font_gen.py` builds the font tables |
| `test/` | Host unit tests (plain CMake + gcc, no pico-sdk) |
| `lib/pico-sdk`, `lib/PicoDVI` | Pinned submodules |
| `assets/fonts/` | Terminus Font 8x16 (SIL OFL 1.1) and misc-fixed 5x8 (public domain); see its README |
| `cmake/build_info.cmake` | Generates `term_build.h` (git describe, commit, date) on every build |
| `cmake/fonts.cmake` | Font table generation, shared by the firmware and host tests |
| `scripts/` | `setup-toolchain.sh`, `build.sh`, `test.sh`, `package.sh`, `changelog.sh` |
| `toolchain.env` | Pinned CMake, Arm GNU Toolchain and picotool versions |
| `docs/FLASHING.txt` | End-user flashing steps, shipped in the package |

## Fonts and glyph codes

The screen buffer stores one-byte glyph codes (see `src/font.h`): ASCII at
0x20-0x7E, Latin-1 at 0xA0-0xFF, DEC Special Graphics (line drawing) at
0x01-0x1F, and shades/blocks/double-line box drawing at 0x80-0x9F. The
tables are generated at build time: 8x16 from Terminus Font for 80 columns,
and 5x16 (misc-fixed 5x8 doubled) for 132. Glyphs a font lacks (control
pictures, scan lines, half blocks) are drawn by `font_gen.py`.

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
selects the pico-sdk board and PicoDVI pin configuration. The firmware is
built to run entirely from SRAM (`COPY_TO_RAM`, default ON), as PicoDVI's
apps are: the board's boot2 reads flash with the slow 03h command at
clk_sys / 4, and at DVI clocks above 252 MHz flash execution hangs (bd
`dvi-feather-terminal-dmt.10`). `-DVIDEO_DIAG=ON` adds bring-up diagnostics
on UART0 TX (boot markers, core 1 scanline timing, fault reports). Plain `cmake -S .
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
1.1; see `assets/fonts/Terminus-OFL.txt`. The 8x16 table generated from it
is a derivative and stays under that licence. The misc-fixed 5x8 font is
public domain. pico-sdk and PicoDVI are BSD-3-Clause.
