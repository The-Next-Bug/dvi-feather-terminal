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

Status: VT100 emulation on UART0 (115200 8N1, TX = GP0, RX = GP1; optional
RTS/CTS on GP3/GP2) in rows 0-23, with row 24 as a status line (bytes
waiting in the receive and transmit buffers, line errors, and hold-offs with
flow control). `src/vtparse.c` is a DEC-compatible escape sequence parser
(Paul Williams' state machine); `src/term.c` implements cursor movement,
scrolling regions, erase, insert/delete (VT102), tab stops, G0/G1 character
sets (ASCII, UK, DEC Special Graphics), modes (DECAWM, DECOM, IRM, LNM,
DECCKM, DECCOLM 80/132, DECTCEM), save/restore cursor, SGR colours (8/16,
256 and direct, reduced to RGB222), DECALN, RIS, and replies to DA, DSR/CPR
and DECREQTPARM on UART0 TX. Underline, blink and reverse screen (DECSCNM)
are rendered. Not yet: double-size lines, VT52 mode, the alternate screen, a
keyboard.

### Wiring

| Feather | Signal | Serial adapter |
|---|---|---|
| TX (GP0) | data out | RX |
| RX (GP1) | data in | TX |
| GND | | GND |

3.3 V logic only.

### Flow control (optional, off by default)

The default build has no flow control. At 115200 baud the terminal keeps up
with ordinary output, but a long burst that scrolls the whole screen can
overrun it (the status line's `rx buf` then sits near 4096). To use
hardware RTS/CTS:

1. Wire the handshake lines (these are the STEMMA QT pins, so that port is
   then not available for I2C):

   | Feather | Signal | Serial adapter |
   |---|---|---|
   | SCL (GP3) | RTS out: terminal ready to receive | CTS |
   | SDA (GP2) | CTS in: host ready to receive | RTS |

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

At startup a demo alternates between 80 and 132 columns every 5 seconds
until the first byte arrives on UART0, which starts the terminal (build
with `-DSTARTUP_DEMO=OFF` to skip it). Work is tracked with `bd` (beads):
`bd list`.

## Trying it without a keyboard

`tools/hostbridge.py` runs a program on this PC with the terminal as its
display: its output goes out over the serial adapter, and your keystrokes
here plus the terminal's replies come back as its input.

```sh
tools/hostbridge.py /dev/ttyUSB0 vttest   # or: vim, top, $SHELL ...
# add --rtscts when the terminal is built with flow control
```

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware. Portable and host-tested: `screen.[ch]` (cell buffer), `vtparse.[ch]` (escape sequence parser), `term.[ch]` (VT100 emulation), `raster.[ch]` (cells to pixels), `demo.[ch]`. RP2040-specific: `video.[ch]` (libdvi, core 1), `serial.[ch]` (UART with RTS/CTS and a TX queue). `font_gen.py` builds the font tables |
| `test/` | Host unit tests (plain CMake + gcc, no pico-sdk) |
| `tools/hostbridge.py` | Run a host program with the terminal as its display (see above) |
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

`scripts/build.sh` takes `TARGET`, which selects the pico-sdk board and
PicoDVI pin configuration:

| `TARGET` | Board | DVI pins (PicoDVI config) |
|---|---|---|
| `feather-rp2040-dvi` (default) | Adafruit Feather RP2040 DVI | D0-D2 GP18/20/22, clock GP16, inverted pairs (`adafruit_feather_dvi_cfg`) |
| `pico2-dvi-sock` | Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock | D0-D2 GP12/18/16, clock GP14 (`pico_sock_cfg`) |

Both use UART0 on GP0/GP1 (and GP2/GP3 for optional RTS/CTS), run the
same firmware at 800x600 (400 MHz, 1.3 V) from SRAM, and are built and
packaged together by CI. The firmware is
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
