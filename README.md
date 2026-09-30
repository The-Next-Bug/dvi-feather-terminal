# dvi-feather-terminal

A VT100 serial terminal with DVI output for RP2040/RP2350 boards: UART in,
800x600 DVI out, 80 or 132 columns by 24 rows plus a status line. It is
meant to become the console for later RP2040/RP2350 "compute" boards (e.g.
RunCPM). On the Pico 2, a USB keyboard on the board's USB port types to the
host over the same UART; `tools/hostbridge.py` drives the host side from a
PC instead.

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
  DECCKM, DECKPAM, DECCOLM 80/132, DECSCNM, DECTCEM, DECBKM), save/restore cursor,
  DECALN, RIS, REP. Replies to DA, DSR/CPR and DECREQTPARM go back on UART0
  TX.
- **Attributes**: SGR bold (bright and emboldened), underline, blink,
  reverse; colours 8/16, 256 and 24-bit, shown with the display's 64 colours
  (RGB222).
- **Keyboard** (Pico 2 only): USB keyboard on the native USB port (host
  mode, see [USB keyboard](#usb-keyboard)); keys go to the host on UART0 TX.
- **Status line** (row 24): width, build id, line settings, `kbd` while a
  keyboard is attached, `local echo` when it is on, and the last key sent to
  the host (`key ESC [A`, `key ^C`, `key DEL`, ...), for checking a keyboard
  without a host.
- **SET-UP** (first version): power-on width (80/132), what Backspace sends
  (DEL/BS) and local echo, kept in flash. F12 on a USB keyboard or `CSI ? 999 ~` from the host opens
  it; see [SET-UP](#set-up).
- **Startup logo**: at power-on/reset the logo (`assets/logo/`) shows for
  10 seconds (monitors take a few to sync), with the build on the status
  line, then the terminal starts; the first byte on UART0 or a key press
  ends it early. See [Startup logo](#startup-logo); `-DLOGO_MS=<ms>` changes
  the time, `-DSTARTUP_LOGO=OFF` skips it.
- **Demo** (opt-in, `-DSTARTUP_DEMO=ON`): after the logo, alternates
  between 80 and 132 columns every 5 seconds until the first byte arrives
  or a key is pressed.

Not yet (tracked in bd): the rest of SET-UP (line settings, colours, ...),
alternate screen, VT52 mode, double-width/height lines, smooth scrolling.

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
overrun it (the 4 KB receive buffer fills and characters go missing). To use
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

## USB keyboard

**Pico 2 (`pico2-dvi-sock`) only.** The Feather RP2040 DVI build leaves the
keyboard out (`USB_KEYBOARD` defaults to OFF there): with it, that board
crashed, and it is not yet known why (bd `dvi-feather-terminal-dmt.20`).
`-DUSB_KEYBOARD=ON` builds it in anyway, for experiments.

Plug a USB keyboard into the Pico 2's micro-USB port through an OTG adapter
or cable; a hub (or a keyboard with one built in) works too. The firmware is
the USB host, so the port is no longer the board's power input, and the
board does not switch 5 V onto the connector's VBUS by itself: feed 5 V into
**VBUS** (pin 40), which is the micro-USB connector's VBUS.

Never connect a PC (or charger) to the USB port while 5 V is fed in this
way: both would drive VBUS. To flash, disconnect the 5 V supply and the
keyboard, then use the USB port as usual (BOOTSEL).

Keys (US layout) send what a VT100 sends: letters honour Caps Lock, Ctrl
makes control characters, Alt sends ESC first (meta). Return sends CR (CR LF
in LNM), Backspace DEL and Ctrl-Backspace BS, or the other way round with
DECBKM (`CSI ? 67 h`; its power-on value is a SET-UP setting). The cursor keys follow DECCKM
(`ESC [ A` / `ESC O A`); F1-F4 are PF1-PF4 (`ESC O P`..`S`). The keypad
follows DECKPAM/DECKPNM while Num Lock is on (keypad + is the VT100's keypad
comma) and is cursor/editing keys when it is off, as on a PC. Home, End,
Insert, Delete, Page Up/Down and F5-F11 send the xterm/VT220 codes
(`ESC [ H`/`F`, `ESC [ 2 ~`, ...). Keys repeat after 0.5 s at about 30 per
second. Caps Lock and Num Lock light the keyboard's LEDs. F12 (and Pause)
open [SET-UP](#set-up) instead of sending anything.

## SET-UP

A panel over the bottom text rows for the settings kept in flash:

| Setting | Values | |
|---|---|---|
| Columns | 80, 132 | the width at power-on |
| Backspace sends | DEL, BS | DECBKM at power-on and after RIS (vttest and CP/M software want BS) |
| Local echo | off, on | keys typed are shown on the terminal as well as sent |

It opens from:

- **F12** (or Pause) on a USB keyboard (Pico 2). While it is open, data from the
  host waits in the receive buffer (with RTS/CTS the host is held off).
- **`CSI ? 999 ~`** (`ESC [ ? 9 9 9 ~`) from the host, a sequence private to
  this terminal. The bytes the host sends next are taken as keys, so it can
  be driven from a PC: `tools/hostbridge.py` sends it on Ctrl-\ and then
  passes your keys to the terminal until Return or Esc.

Up/Down select a setting, Left/Right or Space change it, **Return** saves
it to flash and applies it (a new width clears the screen, as DECCOLM
does), **Esc** (or F12 again) leaves without saving. The panel opens
with the width and Backspace mode currently in use, so Return alone makes a
DECCOLM or DECBKM switch from the host the power-on default.

Settings live in the last two 4 KB sectors of the flash, used in turn with a
sequence number and a CRC, so a save cut short by a power loss keeps the
previous settings; flashing new firmware keeps them too. A save holds up the
terminal for the erase (typically 30-50 ms); the picture stays up, as the
program runs from RAM. The flash is written with the 400 MHz DVI clock
running: its serial clock is kept at 25-50 MHz (see `src/nvflash.c`).

## Startup logo

`LOGO_IMAGE` (CMake, default `assets/logo/ColorLogo-576.png`) is converted
at build time by `src/logo_gen.py` (Python standard library only). It must
be a non-interlaced 8-bit PNG, at most 800x576 (576 is the 24 text rows; the
status line stays below) and a multiple of 8 pixels wide; alpha is
composited over black, and it is shown unscaled, centred.

The display has 4 levels per colour, and per 8 pixels each colour shows at
most two of them, so the image is posterised (the build picks the best pair
per group). To fit in RAM the logo must be a single hue or grey on black:
every colour channel that is not black must show the same levels (the
default is green only). It is stored run-length encoded (about 19 KB for
the default) and unpacked into the text raster's memory while it shows.
With `LOGO_IN_FLASH` (default ON, experimental) the encoded data stays in
flash and is read once at startup, instead of being copied into SRAM with
the rest of the program; `-DLOGO_IN_FLASH=OFF` keeps the old behaviour.

## Trying it from a PC

`tools/hostbridge.py` runs a program on this PC with the terminal as its
display: its output goes out over the serial adapter, and your keystrokes
here (and on a USB keyboard on a Pico 2) plus the terminal's replies
come back as its input. Ctrl-] quits; Ctrl-\ opens [SET-UP](#set-up).

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
  buffer (`src/screen.c`), rasterisation of changed lines into per-line
  pixel buffers (`src/raster.c`, driven by `video_update` in
  `src/video.c`), and on the RP2350 the USB keyboard: TinyUSB host
  (`src/usbkbd.c`) with boot protocol reports turned into VT100 key codes
  (`src/keyboard.c`).
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
| `src/` | Firmware. Portable and host-tested: `screen.[ch]` (cell buffer), `vtparse.[ch]` (escape sequence parser), `term.[ch]` (VT100 emulation), `raster.[ch]` (cells to pixels), `keyboard.[ch]` (HID keys to VT100 codes), `splash.[ch]` (startup logo unpacking), `settings.[ch]` (settings records), `setup.[ch]` (SET-UP panel), `demo.[ch]`. RP2040/RP2350: `video.[ch]` (libdvi, core 1), `serial.[ch]` (UART with optional RTS/CTS and a TX queue), `nvflash.[ch]` (settings sectors, written at the DVI clock), `usbkbd.[ch]` and `tusb_config.h` (TinyUSB host, `USB_KEYBOARD` builds), `diag.[ch]` (VIDEO_DIAG), `main.c`. `font_gen.py` builds the font tables, `logo_gen.py` the logo |
| `test/` | Host unit tests (plain CMake + gcc with ASan/UBSan, no pico-sdk) |
| `tools/hostbridge.py` | Run a host program with the terminal as its display (see above) |
| `lib/pico-sdk`, `lib/PicoDVI` | Pinned submodules (plus pico-sdk's `lib/tinyusb`) |
| `assets/logo/` | The startup logo (PNG used by the build, JPEG original) |
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
git -C lib/pico-sdk submodule update --init --depth 1 lib/tinyusb   # the one we need
scripts/setup-toolchain.sh           # CMake, Arm GNU Toolchain, picotool
scripts/test.sh                      # host unit tests
scripts/build.sh                     # -> build/feather-rp2040-dvi/dvi_terminal.uf2, build-info.txt
TARGET=pico2-dvi-sock scripts/build.sh   # -> build/pico2-dvi-sock/...
scripts/package.sh                   # every built target -> dist/dvi-terminal-<rev>.zip, dist/CHANGES.md
```

`scripts/build.sh` takes `TARGET` (see the board table above),
`SERIAL_FLOW_CONTROL` and `CMAKE_ARGS` (extra configure arguments, e.g.
`CMAKE_ARGS=-DVIDEO_DIAG=ON`, or `-DUSB_KEYBOARD=ON/OFF` to override the
per-chip default: ON for RP2350, OFF for RP2040). Plain `cmake -S . -B build/x` also works and
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
