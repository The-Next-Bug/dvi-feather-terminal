# dvi-feather-terminal

A VT100 serial terminal with DVI output for RP2040/RP2350 boards: UART in,
800x600 DVI out, 80 or 132 columns by 24 rows plus a status line. It is
meant to become the console for later RP2040/RP2350 "compute" boards (e.g.
RunCPM). A USB keyboard is planned; until then the host side can be driven
with `tools/hostbridge.py`.

Supported boards (both verified on hardware):

| `TARGET` | Board | DVI pins (PicoDVI config) |
|---|---|---|
| `feather-rp2040-dvi` (default) | [Adafruit Feather RP2040 DVI](https://www.adafruit.com/product/5710) | D0-D2 GP18/20/22, clock GP16, inverted pairs (`adafruit_feather_dvi_cfg`) |
| `pico2-dvi-sock` | Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock | D0-D2 GP12/18/16, clock GP14 (`pico_sock_cfg`) |

Releases (`vX.Y.Z` tags) ship both firmware images; see `docs/FLASHING.txt`.

## Features

- **Display**: 800x600p60 over DVI/HDMI. 80 columns use Terminus 8x16 in
  8-pixel cells (centred, 80-pixel borders); 132 columns use misc-fixed 6x13
  in 6-pixel cells. 24-pixel rows; line drawing, block and shade characters
  join up between rows.
- **Emulation**: VT100/VT102. Cursor movement, scrolling regions, erase,
  insert/delete lines and characters, tab stops, G0/G1 character sets (ASCII,
  UK, DEC Special Graphics) with SI/SO, modes (DECAWM, DECOM, IRM, LNM,
  DECCKM, DECKPAM, DECCOLM 80/132, DECSCNM, DECTCEM), save/restore cursor,
  DECALN, RIS, REP. Replies to DA, DSR/CPR and DECREQTPARM go back on UART0
  TX.
- **Attributes**: SGR bold (bright and emboldened), underline, blink,
  reverse; colours 8/16, 256 and 24-bit, shown with the display's 64 colours
  (RGB222).
- **Status line** (row 24): width, build id, line settings, bytes waiting in
  the receive and transmit buffers, line errors (and hold-offs with flow
  control).
- **Startup demo**: alternates between 80 and 132 columns every 5 seconds
  until the first byte arrives on UART0, then the terminal starts (build
  with `-DSTARTUP_DEMO=OFF` to skip it).

Not yet (tracked in bd): USB keyboard, SET-UP screen, alternate screen,
VT52 mode, double-width/height lines, smooth scrolling.

Checked with `vttest` (menus 1-3, 6 and 8) through `tools/hostbridge.py`.

## Wiring

| Board pin | Signal | Serial adapter |
|---|---|---|
| TX (GP0) | data out | RX |
| RX (GP1) | data in | TX |
| GND | | GND |

UART0, 115200 8N1, 3.3 V logic only.

### Flow control (optional, off by default)

The default build has no flow control. At 115200 baud the terminal keeps up
with ordinary output, but a long burst that scrolls the whole screen can
overrun it (the status line's `rx buf` then sits near 4096). To use
hardware RTS/CTS:

1. Wire the handshake lines (on the Feather these are the STEMMA QT pins,
   so that port is then not available for I2C):

   | Board pin | Signal | Serial adapter |
   |---|---|---|
   | GP3 (Feather SCL) | RTS out: terminal ready to receive | CTS |
   | GP2 (Feather SDA) | CTS in: host ready to receive | RTS |

2. Build with flow control on:
   ```sh
   SERIAL_FLOW_CONTROL=ON scripts/build.sh
   ```
   `scripts/build.sh` passes the setting on every run (default `OFF`), and
   records it in `build-info.txt`. For a plain CMake build, configure with
   `-DSERIAL_FLOW_CONTROL=ON`.
3. Enable RTS/CTS on the host side too: `stty -F /dev/ttyUSB0 crtscts`, or
   `tools/hostbridge.py --rtscts ...`.

The status line then shows `RTS/CTS` and a `hold` count of how often the
terminal paused the host. Do not enable it without the wires: the Feather
has pull-ups on SDA/SCL, so an unconnected CTS reads as "hold" and the
terminal would never send its replies (device attribute and cursor position
reports) to the host.

## Trying it without a keyboard

`tools/hostbridge.py` runs a program on this PC with the terminal as its
display: its output goes out over the serial adapter, and your keystrokes
here plus the terminal's replies come back as its input. Ctrl-] quits.

```sh
tools/hostbridge.py /dev/ttyUSB0 vttest   # or: vim, top, $SHELL ...
tools/hostbridge.py --cols 132 /dev/ttyUSB0 $SHELL
# add --rtscts when the terminal is built with flow control
```

It sets `TERM=xterm-color` (8 colours, nothing the terminal lacks);
`--term vt100` turns colour off, `--term xterm-16color` adds bright colours.

## How it works

Video uses [PicoDVI](https://github.com/Wren6991/PicoDVI)'s `libdvi` and
its 2bpp text encoder at 800x600p60 (400 MHz, 1.3 V).

- **Core 0** runs the terminal: UART0 receive interrupt into a ring buffer
  (`src/serial.c`), the escape sequence parser (`src/vtparse.c`, Paul
  Williams' DEC state machine), the VT100 emulation (`src/term.c`) on a cell
  buffer (`src/screen.c`), and rasterisation of changed lines into per-line
  pixel buffers (`src/raster.c`, driven by `video_update` in
  `src/video.c`).
- **Core 1** encodes those buffers to TMDS one scanline at a time and draws
  the blinking cursor (`src/video.c`).

The encoder has two colour levels per 8-pixel group and colour plane, so in
132 columns (where cells straddle groups) colours are exact as long as the
cells sharing a group show at most two levels per plane — true for normal
text on one background and for reverse video. 80 columns is always exact.

The firmware runs entirely from SRAM (`COPY_TO_RAM`, default ON), as
PicoDVI's apps do: the Feather's boot2 reads flash with the slow 03h command
at clk_sys / 4, and at DVI clocks above 252 MHz flash execution hangs (bd
`dvi-feather-terminal-dmt.10`). `-DVIDEO_DIAG=ON` adds bring-up diagnostics
on UART0 TX (boot markers, core 1 scanline timing, fault reports).

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware. Portable and host-tested: `screen.[ch]` (cell buffer), `vtparse.[ch]` (escape sequence parser), `term.[ch]` (VT100 emulation), `raster.[ch]` (cells to pixels), `demo.[ch]`. RP2040/RP2350: `video.[ch]` (libdvi, core 1), `serial.[ch]` (UART with optional RTS/CTS and a TX queue), `diag.[ch]` (VIDEO_DIAG), `main.c`. `font_gen.py` builds the font tables |
| `test/` | Host unit tests (plain CMake + gcc with ASan/UBSan, no pico-sdk) |
| `tools/hostbridge.py` | Run a host program with the terminal as its display (see above) |
| `lib/pico-sdk`, `lib/PicoDVI` | Pinned submodules |
| `assets/fonts/` | Terminus Font 8x16 (SIL OFL 1.1) and misc-fixed 6x13 (public domain); see its README |
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
6x13 from misc-fixed for 132. Glyphs a font lacks (control pictures, scan
lines, half blocks) are drawn by `font_gen.py`.

## Building locally

Everything installs under `.toolchain/` in the repo (git-ignored); only
`curl`, `git`, `python3`, `xz` and a host C/C++ compiler with `make` are
needed.

```sh
git submodule update --init          # pico-sdk, PicoDVI (not their submodules)
scripts/setup-toolchain.sh           # CMake, Arm GNU Toolchain, picotool
scripts/test.sh                      # host unit tests
scripts/build.sh                     # -> build/feather-rp2040-dvi/dvi_terminal.uf2, build-info.txt
TARGET=pico2-dvi-sock scripts/build.sh   # -> build/pico2-dvi-sock/...
scripts/package.sh                   # every built target -> dist/dvi-terminal-<rev>.zip, dist/CHANGES.md
```

`scripts/build.sh` takes `TARGET` (see the board table above),
`SERIAL_FLOW_CONTROL` and `CMAKE_ARGS` (extra configure arguments, e.g.
`CMAKE_ARGS=-DVIDEO_DIAG=ON`). Plain `cmake -S . -B build/x` also works and
picks up `.toolchain/` automatically.

Toolchain versions are pinned in **`toolchain.env`**, the single source of
truth for `scripts/setup-toolchain.sh` and the CI cache key. To upgrade,
change the version and its SHA256 there. pico-sdk and PicoDVI are upgraded
by moving their submodules.

## CI

- `test` — host unit tests (JUnit report).
- `build` — firmware per target (matrix: both boards); UF2/BIN/ELF/
  disassembly/map and `build-info.txt` artifacts.
- `package` — ZIP with UF2 and BIN per target, `FLASHING.txt`, `CHANGES.md`
  (commits since the previous tag), `SHA256SUMS` and the source of the built
  commit. Automatic on the default branch and tags.
- `release` — tags only: uploads the ZIP to the project's generic package
  registry (`dvi-terminal/<tag>`) and creates a GitLab Release linking to it,
  with `CHANGES.md` as the description.

## Releasing

```sh
git tag -a v0.2.0 -m "dvi-feather-terminal 0.2.0"
git push origin v0.2.0
```

Work is tracked with `bd` (beads): `bd ready`, `bd list`.

## Licences

Terminus Font is © Dimitar Toshkov Zhekov under the SIL Open Font License
1.1; see `assets/fonts/Terminus-OFL.txt`. The 8x16 table generated from it
is a derivative and stays under that licence. The misc-fixed 6x13 font is
public domain. pico-sdk and PicoDVI are BSD-3-Clause.
