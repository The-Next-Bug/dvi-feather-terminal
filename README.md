# dvi-feather-terminal

A VT100 serial terminal with DVI output for RP2350 boards: UART in,
800x600 DVI out, 80 or 132 columns by 24 rows plus a status line. It is
meant to become the console for later "compute" boards (e.g. RunCPM). A USB
keyboard on the board's USB port types to the host over the same UART;
`tools/hostbridge.py` drives the host side from a PC instead.

Supported board (verified on hardware):

| `TARGET` | Board | DVI pins (HSTX) |
|---|---|---|
| `pico2-dvi-sock` (default) | Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock | D0 GP12/13, D1 GP18/19, D2 GP16/17, clock GP14/15 (HSTX bits 0-7) |

The name comes from where the project started: it was first built for the
[Adafruit Feather RP2040 DVI](https://www.adafruit.com/product/5710), and
that board was supported up to v0.4.0. The firmware has since outgrown the
RP2040 (SRAM, and the USB keyboard crashed it at the DVI clock), so RP2040
builds were dropped; the name stayed.

Releases (`vX.Y.Z` tags) ship the firmware image; see `docs/FLASHING.txt`.

## Features

- **Display**: 800x600p60 over DVI/HDMI. 80 columns use Terminus 8x16 in
  8-pixel cells (centred, 80-pixel borders); 132 columns use misc-fixed 6x13
  in 6-pixel cells (792 pixels, centred). Every cell shows its own colours
  exactly, in both widths. 24-pixel
  rows; line drawing, block and shade characters join up between rows.
- **Emulation**: VT100/VT102. Cursor movement, scrolling regions, erase,
  insert/delete lines and characters, tab stops, G0/G1 character sets (ASCII,
  UK, DEC Special Graphics) with SI/SO, modes (DECAWM, DECOM, IRM, LNM,
  DECCKM, DECKPAM, DECCOLM 80/132, DECSCNM, DECTCEM, DECBKM), save/restore cursor,
  DECALN, RIS, REP. Replies to DA, DSR/CPR and DECREQTPARM go back on UART0
  TX.
- **Attributes**: SGR bold (bright and emboldened), underline, blink,
  reverse; colours 8/16, 256 and 24-bit, shown with the display's 64 colours
  (RGB222).
- **Keyboard**: USB keyboard on the native USB port (host
  mode, see [USB keyboard](#usb-keyboard)); keys go to the host on UART0 TX.
- **Status line** (row 24): width, build id, line settings, `kbd` while a
  keyboard is attached, `echo` when local echo is on, and the last key sent to
  the host (`key ESC [A`, `key ^C`, `key DEL`, ...), for checking a keyboard
  without a host.
- **SET-UP**: power-on width, Backspace (DEL/BS), local echo, baud rate,
  data/parity/stop bits, new line mode, colors and a mono palette, kept in
  flash. F12 on a USB keyboard or `CSI ? 999 ~` from the host opens it; see
  [SET-UP](#set-up).
- **Startup logo**: at power-on/reset the logo (`assets/logo/`) shows for
  10 seconds (monitors take a few to sync), with the build on the status
  line, then the terminal starts; the first byte on UART0 or a key press
  ends it early. See [Startup logo](#startup-logo); `-DLOGO_MS=<ms>` changes
  the time, `-DSTARTUP_LOGO=OFF` skips it.
- **Z80 CP/M machine** (SET-UP Mode): instead of a serial terminal, the
  screen and keyboard can be the console of a built-in Z80 computer running
  CP/M 2.2 (and CP/M 3 / MP/M II disk images) from a microSD card, on
  [z80pack](https://github.com/udo-munk/z80pack)'s CPU core. See
  [Z80 CP/M machine](#z80-cpm-machine).
- **Demo** (opt-in, `-DSTARTUP_DEMO=ON`): after the logo, alternates
  between 80 and 132 columns every 5 seconds until the first byte arrives
  or a key is pressed.

Not yet (tracked in bd): flow control as a SET-UP setting, an ADM-3A
terminal type, alternate screen, VT52 mode, double-width/height lines,
smooth scrolling.

Checked with `vttest` (menus 1-3, 6 and 8) through `tools/hostbridge.py`.

## Wiring

| Board pin | Signal | Serial adapter |
|---|---|---|
| TX (GP0) | data out | RX |
| RX (GP1) | data in | TX |
| GND | | GND |

UART0, 3.3 V logic only. 115200 8N1 until changed in [SET-UP](#set-up).

For the [Z80 CP/M machine](#z80-cpm-machine), a microSD adapter goes on
SPI0: GP4 (pin 6) MISO, GP5 (pin 7) CS, GP6 (pin 9) SCK, GP7 (pin 10)
MOSI, plus 3V3 (pin 36) and GND; see [SD card wiring](#sd-card-wiring).

### Flow control (optional, off by default)

The default build has no flow control. At 115200 baud the terminal keeps up
with ordinary output, but a long burst that scrolls the whole screen can
overrun it (the 4 KB receive buffer fills and characters go missing). To use
hardware RTS/CTS:

1. Wire the handshake lines:

   | Board pin | Signal | Serial adapter |
   |---|---|---|
   | GP3 | RTS out: terminal ready to receive | CTS |
   | GP2 | CTS in: host ready to receive | RTS |

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
terminal paused the host. Do not enable it without the wires: an
unconnected CTS only floats on the pin's weak pull, and if it reads as
"hold" the terminal never sends its replies (device attribute and cursor
position reports) to the host.

## USB keyboard

Built in by default; `-DUSB_KEYBOARD=OFF` leaves it out.

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
| Baud rate | 300 - 115200 | default 115200 |
| Data/parity/stop | 8N1, 8E1, 8O1, 8N2, 7E1, 7O1, 7N1, 7N2 | default 8N1 |
| New line (LNM) | off, on | LNM at power-on and after RIS: Return sends CR LF, and LF also returns the cursor |
| Colors | white, green, amber, blue | text colors: white, or green, amber or blue phosphor, on black; bold is one level brighter; the status line, SET-UP panel and startup logo match (white keeps the logo's own colors). Changing them recolors what is on screen |
| Palette | color, mono | mono shows SGR colors (the 16 ANSI ones, 256-color and 24-bit) only as brightness within the Colors setting's colors: bright colors (and bold) in the bright shade; a background other than black as inverse (bright ones in the bright shade), text on it in the background color. The status line and SET-UP panel become the text colors reversed, and with white the startup logo shows in grey. Text already on screen keeps its colors |
| Mode | terminal, CP/M machine | what the screen and keyboard are for: a serial terminal on UART0, or the console of the [Z80 CP/M machine](#z80-cpm-machine). Saving a new mode restarts the board |

Under Palette, two rows preview both color settings: default text, bold and
the status line, then each of the 16 ANSI colors (0-F) as text on the
default background and, below it, as a background under default text.

It opens from:

- **F12** (or Pause) on a USB keyboard. While it is open, data from the
  host waits in the receive buffer (with RTS/CTS the host is held off).
- **`CSI ? 999 ~`** (`ESC [ ? 9 9 9 ~`) from the host, a sequence private to
  this terminal. The bytes the host sends next are taken as keys, so it can
  be driven from a PC: `tools/hostbridge.py` sends it on Ctrl-\ and then
  passes your keys to the terminal until Return or Esc.

Up/Down select a setting, Left/Right or Space change it, **Return** saves
it to flash and applies it (a new width clears the screen, as DECCOLM
does), **Esc** (or F12 again) leaves without saving. The panel opens
with the width, Backspace mode and LNM currently in use, so Return alone
makes a DECCOLM, DECBKM or LNM switch from the host the power-on default.
A new baud rate or framing applies as soon as it is saved: the host has to
be switched to match (opened from the host, SET-UP's own keys then arrive
at the new settings too).

Settings live in the two 4 KB sectors before the last one of the flash, used
in turn with a sequence number and a CRC, so a save cut short by a power loss
keeps the previous settings; flashing new firmware keeps them too. (The last
sector is erased by every UF2 flash: the SDK's RP2350-E10 workaround block
for 0x10ffff00 wraps round to it.) A save holds up the
terminal for the erase (typically 30-50 ms); the picture stays up, as the
program runs from RAM. The flash is written with the 400 MHz DVI clock
running: the ROM's erase/program routines run its serial clock at
clk_sys / 12, and reads are set back to at most 50 MHz afterwards (see
`src/nvflash.c`).

## Z80 CP/M machine

With **Mode** set to *CP/M machine* in SET-UP, the board is a small CP/M
computer: a Z80 emulated on core 0 (z80pack's CPU core, as in its
[picosim](https://github.com/udo-munk/z80pack/tree/master/picosim) for
the Pico) with the terminal as its console, the USB keyboard as its
keyboard, disk images on a microSD card, and UART0 as its second serial
port. Set Mode back to *terminal* for the plain serial terminal.

### SD card wiring

An SPI microSD adapter (3.3 V) on SPI0:

| Board pin | Card |
|---|---|
| GP4 (pin 6) | MISO / DO |
| GP5 (pin 7) | CS |
| GP6 (pin 9) | SCK / CLK |
| GP7 (pin 10) | MOSI / DI |
| 3V3 (pin 36) | VCC |
| GND | GND |

The card must have a FAT file system (FAT12/16/32; cards up to 32 GB come
as FAT32; format bigger, exFAT, cards as FAT32 on a PC).

### Drives and disk images

The machine has eight drives, A-H. Its disks are image files in `/DISKS80`
on the card, so a PC can copy them on and off, in three formats, told apart
by their size (all 128-byte sectors, stored track by track):

| Format | Size | Geometry | CP/M capacity | cpmtools |
|---|---|---|---|---|
| 8" floppy (z80pack picosim) | 256256 bytes | 77 tracks x 26 sectors | 241 KB, 64 files | `ibm-3740` |
| 4 MB hard disk (z80pack cpmsim) | 4177920 bytes | 255 x 128 | 4080 KB, 1024 files | `z80pack-hd` |
| 8 MB hard disk | 8355840 bytes | 256 x 255 | 8160 KB, 1024 files | `dvi-hd8` |

cpmtools knows the first two; the 8 MB one is in `docs/cpmtools-diskdefs`:
add it to cpmtools' `diskdefs` file, then e.g. `cpmls -f dvi-hd8 DISK.DSK`
(checked with cpmtools 2.23 on images written by the emulated CP/M). Drive A must hold a bootable 8"
floppy: CP/M reloads itself from its system tracks on every warm boot. The
built-in `CPM22` is a 62K CP/M 2.2 with a BIOS for the eight drives and the
three formats (`src/cpm`); z80pack's CP/M 3 and MP/M II images keep their
own BIOSes, with four floppy drives.

### The monitor

At power-on in CP/M mode the monitor mounts the card and boots from drive
A after 3 seconds (any key stops that), or shows its menu:

- **Return**: boot from drive A (picosim's boot ROM loads track 0 sector 1).
- **M**: mount a disk image on drive A-H (by name, without `.DSK`;
  Return alone empties the drive). The assignments are kept in
  `/DISKS80/DRIVES.CFG`.
- **L**: list the images on the card, with their formats.
- **N**: make a new blank disk: name, then 8" floppy, 4 MB or 8 MB
  (an 8 MB one takes a few seconds to write).
- **I**: install the built-in disk images onto the card: `CPM22` (CP/M 2.2
  with its tools: boots), `Z80TESTS` (Z80 instruction exercisers and
  `CPUTEST`, a benchmark) and `EMPTY` (a blank disk). They are kept
  compressed in flash (about 240 KB) to start a blank card with; existing
  files are only replaced if you say so.
- **S**: CPU speed test (a JP loop for a second, in Z80 MHz).
- **C**: check the card again (after changing it).
- **R**: resume the CPU, after leaving it from SET-UP.

In CP/M mode the New line (LNM) setting is not used: Return sends CR
alone, as CP/M expects (a following LF would wait as a key press, which
makes DIR and TYPE stop after their first line).

While CP/M runs, F12 opens SET-UP as usual (the CPU waits). In CP/M mode
it has two more keys: **D** opens the Drives panel, which shows what is
mounted on A-H and changes it (Up/Down a drive, Left/Right or Space a disk
image from the card, Return mounts and saves); CP/M sees a changed drive
after a warm boot, so type **^C** at the prompt afterwards (as after
swapping floppies; don't change a drive a program is using). **M** returns
to the monitor. **F11** is the reset button: it reboots from drive
A at once (keys typed ahead are dropped; with drive A empty it goes to the
monitor). In terminal mode F11 sends its usual `ESC [ 2 3 ~`. `BYE` stops
the machine, `RESET` reboots it (z80pack's hardware control port).

The status line shows the drives as letters (`-` for an empty one; the
Drives panel names them) and the emulated clock rate (MHz). A drive's letter
lights up (reverse video) while it is being read or written, like a drive's
busy light.

The machine is picosim's: ports 0/1 console, 2/3 the second serial port
(UART0 at the SET-UP baud rate), 4 the disk controller, 8 the format of
the disk in a drive (for the BIOS; not picosim's), 5/6 a printer
(none), 64 the MMU (bank 0 plus `MACHINE_BANKS` 48 KB banks, default 2:
enough for CP/M 3; MP/M II wants as many as it was generated for), 65/66 a
clock (no RTC: it starts at 1 Jan 2026 at power-on), 67 a 60 Hz timer
interrupt and 160 the hardware control port. The CPU runs unthrottled in
10 ms slices between the keyboard, disk and screen work: the monitor's
speed test (a JP loop) measured 48.84 MHz (Z80 clock equivalent) on a Pico 2
at the 400 MHz DVI clock, and the status line shows about 36 MHz while
`EXZ80DOC` runs (its prefixed and block instructions cost more per
T-state).

## Startup logo

`LOGO_IMAGE` (CMake, default `assets/logo/ColorLogo-576.png`) is converted
at build time by `src/logo_gen.py` (Python standard library only). It must
be a non-interlaced 8-bit PNG, at most 800x576 (576 is the 24 text rows; the
status line stays below) and a multiple of 8 pixels wide; alpha is
composited over black, and it is shown unscaled, centred.

The image is posterised to 4 levels per colour (and, a leftover of the
PicoDVI encoder, at most two levels per 8-pixel group). The logo must be a
single hue or grey on black: every colour channel that is not black must
show the same levels (the default is green only). It is stored run-length
encoded (about 19 KB for the default); while it shows, a copy sits in the
text raster's memory and core 1 decodes it a scanline at a time. The saved
Colors setting tints it: green, amber and blue show it in that hue (each of
its four levels gets a colour), white shows it as it is.
With `LOGO_IN_FLASH` (default ON, experimental) the encoded data stays in
flash and is copied out only while the logo shows, instead of being copied
into SRAM with the rest of the program; `-DLOGO_IN_FLASH=OFF` keeps the old
behaviour.

## Trying it from a PC

`tools/hostbridge.py` runs a program on this PC with the terminal as its
display: its output goes out over the serial adapter, and your keystrokes
here (and on a USB keyboard on the terminal) plus the terminal's replies
come back as its input. Ctrl-] quits; Ctrl-\ opens [SET-UP](#set-up).

```sh
tools/hostbridge.py /dev/ttyUSB0 vttest   # or: vim, top, $SHELL ...
tools/hostbridge.py --cols 132 /dev/ttyUSB0 $SHELL
# add --rtscts when the terminal is built with flow control
```

It sets `TERM=xterm-color` (8 colours, nothing the terminal lacks);
`--term vt100` turns colour off, `--term xterm-16color` adds bright colours.

## How it works

Video comes from the RP2350's HSTX, whose TMDS encoder turns RGB565 pixels
into DVI (`src/hstx_dvi.c`, after Raspberry Pi's
`pico-examples/hstx/dvi_out_hstx_encoder`): 800x600 at 60 Hz, a 40 MHz pixel
clock, so clk_hstx 200 MHz and clk_sys 400 MHz (1.3 V). Two DMA channels
feed it blanking command lists and scanlines in turn. `-DHSTX_MODE=1` (56 Hz,
clk_hstx 180 MHz) and `2` (reduced blanking, 177 MHz) are slower fallbacks;
`hstx_test.uf2`, built alongside, shows a test pattern for checking a
monitor and the clocks.

- **Core 0** runs the terminal: UART0 receive interrupt into a ring buffer
  (`src/serial.c`), the escape sequence parser (`src/vtparse.c`, Paul
  Williams' DEC state machine), the VT100 emulation (`src/term.c`) on a cell
  buffer (`src/screen.c`), rasterisation of changed lines (glyph bits per
  cell with the attributes applied, and each cell's colours:
  `src/raster.c`, driven by `video_update` in `src/video.c`), and the USB
  keyboard: TinyUSB host
  (`src/usbkbd.c`) with boot protocol reports turned into VT100 key codes
  (`src/keyboard.c`).
- **Core 1** composes each scanline from those into RGB565 pixels a couple
  of lines ahead of the DMA, inverts the cursor cell in the blink-on phase,
  and decodes the startup logo (`src/video.c`, `raster_compose`); the DMA
  interrupt runs on core 1 too.

The firmware runs entirely from SRAM (`COPY_TO_RAM`, default ON): the
boot-time flash (XIP) setup is chosen for clk_sys up to 150 MHz, far below the 400 MHz DVI clock (bd `dvi-feather-terminal-dmt.10`).
`-DVIDEO_DIAG=ON` adds bring-up diagnostics on UART0 TX (boot markers, core 1
scanline timing, fault reports).

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware. Portable and host-tested: `screen.[ch]` (cell buffer), `vtparse.[ch]` (escape sequence parser), `term.[ch]` (VT100 emulation), `raster.[ch]` (cells to pixels), `keyboard.[ch]` (HID keys to VT100 codes), `splash.[ch]` (startup logo decoding), `settings.[ch]` (settings records), `setup.[ch]` (SET-UP panel), `demo.[ch]`, and for the Z80 machine `machine.[ch]` (z80pack glue; `z80pack/` holds its configuration headers), `monitor.[ch]`, `drives.[ch]` (disk images on FatFs), `drivepanel.[ch]` (SET-UP's Drives panel), `cpm/` (the CP/M 2.2 BIOS and boot loader, see its README), `diskimg.[ch]` and `lz.[ch]` (the built-in images), `crc32.[ch]`. RP2350: `sdcard.[ch]` (SPI SD card for FatFs), `hstx_dvi.[ch]` (HSTX DVI output), `video.[ch]` (scanlines on core 1), `hstx_test.c` (test pattern), `serial.[ch]` (UART with optional RTS/CTS and a TX queue), `nvflash.[ch]` (settings sectors, written at the DVI clock), `usbkbd.[ch]` and `tusb_config.h` (TinyUSB host, `USB_KEYBOARD` builds), `diag.[ch]` (VIDEO_DIAG), `main.c`. `font_gen.py` builds the font tables, `logo_gen.py` the logo, `diskimg_gen.py` the compressed disk images |
| `test/` | Host unit tests (plain CMake + gcc with ASan/UBSan, no pico-sdk) |
| `tools/hostbridge.py` | Run a host program with the terminal as its display (see above) |
| `lib/pico-sdk` | Pinned submodule (plus its `lib/tinyusb`) |
| `lib/z80pack` | Vendored part of z80pack (Z80 core, z80asm, picosim disk images); `UPSTREAM.md` gives the commit and local changes |
| `lib/fatfs` | ChaN's FatFs R0.15a, vendored (see its README) |
| `assets/logo/` | The startup logo (PNG used by the build, JPEG original) |
| `assets/fonts/` | Terminus Font 8x16 (SIL OFL 1.1) and misc-fixed 6x13 (public domain); see its README |
| `cmake/build_info.cmake` | Generates `term_build.h` (git describe, commit, date) on every build |
| `cmake/fonts.cmake` | Font table generation, shared by the firmware and host tests |
| `scripts/` | `setup-toolchain.sh`, `build.sh`, `test.sh`, `package.sh`, `changelog.sh` |
| `toolchain.env` | Pinned CMake, Arm GNU Toolchain and picotool versions |
| `docs/FLASHING.txt` | End-user flashing steps, shipped in the package |
| `docs/THIRD-PARTY.md`, `docs/licenses/` | Third-party components and licences (shipped, with the texts from the submodules, in `licenses/`) |

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
git submodule update --init          # pico-sdk (not its submodules; z80pack is vendored)
git -C lib/pico-sdk submodule update --init --depth 1 lib/tinyusb   # the one we need
scripts/setup-toolchain.sh           # CMake, Arm GNU Toolchain, picotool
scripts/test.sh                      # host unit tests
scripts/build.sh                     # -> build/pico2-dvi-sock/dvi_terminal.uf2, build-info.txt
scripts/package.sh                   # every built target -> dist/dvi-terminal-<rev>.zip, dist/CHANGES.md
```

`scripts/build.sh` takes `TARGET` (see the board table above),
`SERIAL_FLOW_CONTROL` and `CMAKE_ARGS` (extra configure arguments, e.g.
`CMAKE_ARGS=-DVIDEO_DIAG=ON`, or `-DUSB_KEYBOARD=OFF`). Plain `cmake -S . -B build/x` also works and
picks up `.toolchain/` automatically.

Toolchain versions are pinned in **`toolchain.env`**, the single source of
truth for `scripts/setup-toolchain.sh` and the CI cache key. To upgrade,
change the version and its SHA256 there. pico-sdk is upgraded by moving its
submodule.

## CI

- `test` — host unit tests (JUnit report).
- `build` — firmware per target (matrix, one entry per board); UF2/BIN/ELF/
  disassembly/map and `build-info.txt` artifacts.
- `package` — ZIP with UF2 and BIN per target, `FLASHING.txt`,
  `THIRD-PARTY.md` and `licenses/`, `CHANGES.md`
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
public domain. pico-sdk is BSD-3-Clause, as is the pico-examples HSTX DVI
example `src/hstx_dvi.c` follows.

TinyUSB (in pico-sdk) is MIT; pico-sdk's CMSIS headers are Apache-2.0.
z80pack is © Udo Munk and others under the MIT licence (`lib/z80pack/LICENSE`);
`src/machine.c` and `src/z80pack/` follow its picosim, and its CPU core
includes `log.h` (Apache-2.0). FatFs is © ChaN under a 1-clause BSD licence
(`lib/fatfs/LICENSE.txt`). The built-in disk images come from z80pack: CP/M
2.2 is Digital Research's, redistributed under the permission Lineo
(Caldera) gave in 2001 for the CP/M material of "The Unofficial CP/M Web
Site"; the Z80 exercisers are Frank D. Cringle's, GPL-2.0 (their sources are
on the same disk image).

[`docs/THIRD-PARTY.md`](docs/THIRD-PARTY.md) lists every third-party
component in the firmware with its licence; release packages carry it and the
licence texts in `licenses/`.
