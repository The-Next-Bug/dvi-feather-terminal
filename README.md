# dvi-feather-terminal

A VT100 serial terminal with DVI output for the Raspberry Pi Pico 2
(RP2350) with the Pico DVI Sock: UART in, 800x600 DVI out, 80 or 132
columns by 24 rows plus a status line, and a USB keyboard that types to the
host over the same UART. The same board can instead be a small Z80 CP/M
computer with the screen and keyboard as its console and its disks on a
microSD card (see [Z80 CP/M machine](#z80-cpm-machine)).

Releases (`vX.Y.Z` tags) ship the firmware; see
[Getting started](#getting-started).

## Features

- **Display**: 800x600p60 over DVI/HDMI. 80 columns use Terminus 10x20 in
  10-pixel cells across the full width; 132 columns use misc-fixed 6x12 at
  double height (each glyph row on two scanlines, like a VT100) in 6-pixel
  cells (792 pixels, centered). Rows are 24 pixels high; line drawing,
  block and shade characters join up between rows.
- **Emulation**: VT100/VT102. Cursor movement, scrolling regions, erase,
  insert/delete lines and characters, tab stops, G0/G1 character sets
  (ASCII, UK, DEC Special Graphics) with SI/SO, modes (DECAWM, DECOM, IRM,
  LNM, DECCKM, DECKPAM, DECCOLM 80/132, DECSCNM, DECTCEM, DECBKM),
  save/restore cursor, DECALN, RIS, REP. Replies to DA, DSR/CPR and
  DECREQTPARM go back on UART0 TX. Checked with `vttest` (menus 1-3, 6 and
  8) through `tools/hostbridge.py`. Also the VT100's VT52 mode and a Lear
  Siegler ADM-3A (with the Kaypro's additions); see
  [Terminal types](#terminal-types).
- **Attributes**: SGR bold (bright, and in 80 columns the bold face), underline, blink,
  reverse; colors 8/16, 256 (the xterm palette, exact) and 24-bit (the
  nearest of those 256).
- **Keyboard**: a USB keyboard on the Pico 2's USB port; see
  [USB keyboard](#usb-keyboard).
- **Status line** (row 24): the terminal's state and the last key sent, for
  checking a keyboard without a host; in CP/M mode, the drives and the
  emulated clock rate. See [Status line](#status-line).
- **SET-UP**: power-on width, Backspace (DEL/BS), local echo, baud rate,
  data/parity/stop bits, new line mode, colors, palette, mode and terminal
  type, kept in
  flash. F12 or Pause on the USB keyboard, or `CSI ? 999 ~` from the host,
  opens it; see [SET-UP](#set-up).
- **Startup logo**: at power-on/reset the logo shows for 10 seconds
  (monitors take a few to sync), with the build on the status line; the
  first byte on UART0 or a key press ends it early. See
  [Startup logo](#startup-logo).
- **Z80 CP/M machine** (SET-UP Mode): a built-in Z80 computer running CP/M
  2.2 (and z80pack's picosim CP/M 3 images) from a microSD card, on
  [z80pack](https://github.com/udo-munk/z80pack)'s CPU core. See
  [Z80 CP/M machine](#z80-cpm-machine).
- **Demo**: a test screen of colors, fonts and line drawing that alternates
  between 80 and 132 columns every 5 seconds. **T** in SET-UP shows it
  until a key is pressed; with `-DSTARTUP_DEMO=ON` it also runs after the
  logo until the first byte arrives or a key is pressed.

### Limitations

- No alternate screen (`CSI ? 1049 h` and friends): full-screen programs
  such as vim or less leave their last screen behind when they exit.
- Double-width and double-height lines show as normal lines; scrolling is
  jump scrolling only (DECSCLM is ignored).
- Flow control is a build option (RTS/CTS), not a SET-UP setting, and
  there is no XON/XOFF.

## Hardware

A Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock for the DVI/HDMI
connector. Everything else is optional:

| Pico 2 pin | Signal | Connects to | Needed for |
|---|---|---|---|
| GP0 (pin 1) | UART0 TX, data out | serial adapter RX | terminal |
| GP1 (pin 2) | UART0 RX, data in | serial adapter TX | terminal |
| GND | ground | serial adapter GND | terminal |
| GP3 (pin 5) | RTS out: terminal ready to receive | serial adapter CTS | [flow control](#flow-control) builds only |
| GP2 (pin 4) | CTS in: host ready to receive | serial adapter RTS | [flow control](#flow-control) builds only |
| GP4 (pin 6) | SPI0 MISO | SD card DO | [CP/M machine](#z80-cpm-machine) |
| GP5 (pin 7) | SD card CS | SD card CS | CP/M machine |
| GP6 (pin 9) | SPI0 SCK | SD card CLK | CP/M machine |
| GP7 (pin 10) | SPI0 MOSI | SD card DI | CP/M machine |
| 3V3 (pin 36), GND | power | SD card VCC, GND | CP/M machine |
| VBUS (pin 40) | 5 V in | 5 V supply | [USB keyboard](#usb-keyboard) |
| GP12-19 | HSTX: D0 GP12/13, clock GP14/15, D2 GP16/17, D1 GP18/19 | the DVI Sock | always |

The UART uses 3.3 V logic only, at 115200 8N1 until changed in
[SET-UP](#set-up). The SD card adapter must be an SPI one for 3.3 V.

The project's name comes from where it started: it was first built for the
[Adafruit Feather RP2040 DVI](https://www.adafruit.com/product/5710),
supported up to v0.4.0. The firmware outgrew the RP2040 (SRAM, and the USB
keyboard crashed it at the DVI clock), so RP2040 builds were dropped; the
name stayed.

### Other boards

Every pin the firmware uses comes from a board header, `boards/<target>.h`,
built with `TARGET=<target> scripts/build.sh`. `boards/pico2-dvi-sock.h` is
the board above; `boards/example.h` is a template for a new board with each
setting explained:

- **DVI**: the four TMDS pairs must be on GP12-19 (the RP2350's HSTX pins),
  but in any order and either way round. Each pair's + and - pin is set
  separately, so a pair routed with + and - crossed is fixed by swapping
  its two numbers. Drive strength and slew rate can be raised.
- **Terminal UART**: which UART, TX and RX, and the CTS/RTS pins for
  flow control builds.
- **SD card**: which SPI, its MISO/SCK/MOSI, and CS (any GPIO).
- **USB host**: an optional pin that switches on the port's 5 V.
- **LED** (diagnostic builds only), and for a board without a Pico 2
  module, the RP2350 variant, flash size and crystal (a pico-sdk board
  header; the SDK's own boards can be included, as the DVI Sock's does).

`src/board.h` checks the header when it compiles. A missing setting, a pin
that cannot carry its signal, or a pin used twice stops the build with an
error that names the setting (`test/test_board.sh` covers the checks). The
messages that show pins on screen use the header's pins.

### Flow control

The default build has no flow control. At 115200 baud the terminal keeps up
with ordinary output, but a long burst that scrolls the whole screen can
overrun it (the 4 KB receive buffer fills and characters go missing). To use
hardware RTS/CTS:

1. Wire GP2 and GP3 as in the [table above](#hardware).
2. Build with flow control on:
   ```sh
   SERIAL_FLOW_CONTROL=ON scripts/build.sh
   ```
   `scripts/build.sh` passes the setting on every run (default `OFF`), and
   records it in `build-info.txt`. For a plain CMake build, configure with
   `-DSERIAL_FLOW_CONTROL=ON`.
3. Enable RTS/CTS on the host side too: `stty -F /dev/ttyUSB0 crtscts`, or
   `tools/hostbridge.py --rtscts ...`.

The status line then shows `RTS/CTS`. Do not enable it without the wires:
an unconnected CTS only floats on the pin's weak pull, and if it reads as
"hold" the terminal never sends its replies (device attribute and cursor
position reports) to the host.

## Getting started

1. Flash `dvi_terminal-pico2-dvi-sock.uf2` from a release ZIP: hold BOOTSEL
   while plugging the Pico 2 into a PC and copy the file onto the `RP2350`
   drive. `docs/FLASHING.txt` (also in the ZIP) has the details.
2. Connect a DVI/HDMI monitor and a 3.3 V serial adapter (see
   [Hardware](#hardware)). The logo shows, then the terminal starts at
   115200 8N1.
3. Optionally, add a USB keyboard ([USB keyboard](#usb-keyboard)) and open
   [SET-UP](#set-up) with F12 to change the settings, or switch to the
   [Z80 CP/M machine](#z80-cpm-machine).

## Using the terminal

### USB keyboard

Plug a USB keyboard into the Pico 2's micro-USB port through an OTG adapter
or cable; a hub (or a keyboard with one built in) works too. The firmware is
the USB host, so the port is no longer the board's power input, and the
board does not switch 5 V onto the connector's VBUS by itself: feed 5 V into
**VBUS** (pin 40), which is the micro-USB connector's VBUS.

Never connect a PC (or charger) to the USB port while 5 V is fed in this
way: both would drive VBUS. To flash, disconnect the 5 V supply and the
keyboard, then use the USB port as usual (BOOTSEL).

Keys (US layout) send what a VT100 sends: letters honor Caps Lock, Ctrl
makes control characters, Alt sends ESC first (meta). Return sends CR (CR LF
in LNM), Backspace DEL and Ctrl-Backspace BS, or the other way round with
DECBKM (`CSI ? 67 h`; its power-on value is a SET-UP setting). The cursor
keys follow DECCKM (`ESC [ A` / `ESC O A`); F1-F4 are PF1-PF4
(`ESC O P`..`S`). The keypad follows DECKPAM/DECKPNM while Num Lock is on
(keypad + is the VT100's keypad comma) and is cursor/editing keys when it is
off, as on a PC. Home, End, Insert, Delete, Page Up/Down and F5-F11 send the
xterm/VT220 codes (`ESC [ H`/`F`, `ESC [ 2 ~`, ...). Keys repeat after
0.5 s at about 30 per second. Caps Lock and Num Lock light the keyboard's
LEDs. F12 (and Pause) open [SET-UP](#set-up) instead of sending anything.
In VT52 mode and on the ADM-3A the keys send what those terminals send
instead (see [Terminal types](#terminal-types)); SET-UP and the CP/M
monitor always get the VT100 keys.

A USB stick can share the port (through a hub) for
[moving files](#moving-files): up to three sticks next to the keyboard.

### Terminal types

SET-UP's Terminal type picks what the terminal is (in either mode, so CP/M
software set up for one of these works on the built-in Z80 too):

- **VT100**: everything above.
- **VT52**: the VT100's own VT52 mode, which the host can also switch to
  with `CSI ? 2 l` (back with `ESC <`). `ESC A`-`D` move the cursor,
  `ESC H` homes it, `ESC Y row col` (each + 32) addresses it, `ESC J`/`K`
  erase to the end of the screen/line, `ESC I` is a reverse line feed,
  `ESC F`/`G` turn line graphics on/off, `ESC =`/`>` set the keypad mode
  and `ESC Z` answers `ESC / Z`. The cursor keys send `ESC A`-`D`, F1-F4
  `ESC P`-`S`, the keypad in alternate mode `ESC ? x`; keys a VT52 does
  not have (Home, End, Insert, Delete, Page Up/Down, F5-F11) send nothing.
- **ADM-3A**: a Lear Siegler ADM-3A as terminfo's `adm3a` describes it:
  ^H, ^J, ^K and ^L move the cursor (^J scrolls at the bottom), ^Z clears
  the screen, ^^ homes the cursor, `ESC = row col` (each + 32) addresses
  it, and printing wraps at once in the last column. Plus the Kaypro's
  additions (terminfo `kaypro`): ^X and ^W erase to the end of the
  line/screen, `ESC E` and `ESC R` insert/delete a line. No colors or
  attributes, no replies, 80 columns only; the 8th bit of each byte is
  ignored. The cursor keys send ^K ^J ^L ^H and Home ^^; other editing and
  function keys send nothing. The host cannot open SET-UP (`CSI ? 999 ~`)
  in this mode; F12 still does.

`TERMTEST` on the `Z80TESTS` disk shows each type's functions page by page
(see [TERMTEST](#termtest)).

### Status line

Row 24 shows, in terminal mode: the terminal type (`VT100`, `VT52` or
`ADM-3A`) and the width (`80x24`), the build,
the baud rate and framing (`115200 8N1`, plus `RTS/CTS` in flow control
builds), `kbd` while a keyboard is attached, the USB stick (as in CP/M
mode, below), `echo` when local echo is on, and the last key sent to the
host (`key ESC [A`, `key ^C`, `key DEL`, ...).

In CP/M mode it shows `CP/M`, the drives A-H as letters (`-` for an empty
one; a letter lights up in reverse video while that drive is read or
written, like a drive's busy light), `USB` while a USB stick is in use as
`USB:` (lit while it is read or written; `+1` when another stick waits to
take over, `?` when one could not be used, `usb` when none is usable), the
emulated clock rate in MHz (or `monitor` while the monitor runs), `kbd` and
the build.

SET-UP's Status line setting hides it: row 24 then stays blank.

### SET-UP

A box over the bottom text rows for the settings kept in flash. The
selected setting is marked `>` and its name is bold; each current choice is
shown in reverse video:

| Setting | Values | |
|---|---|---|
| Columns | 80, 132 | the width at power-on and after F11 in CP/M mode |
| Backspace sends | DEL, BS | DECBKM at power-on and after RIS (vttest and CP/M software want BS) |
| Local echo | off, on | keys typed are shown on the terminal as well as sent |
| Baud rate | 300 - 115200 | default 115200 |
| Data/parity/stop | 8N1, 8E1, 8O1, 8N2, 7E1, 7O1, 7N1, 7N2 | default 8N1 |
| New line (LNM) | off, on | LNM at power-on and after RIS: Return sends CR LF, and LF also returns the cursor. Not used in CP/M mode |
| Colors | white, green, amber, blue | white: the 16 ANSI colors as on a VGA screen; the others: phosphor shades (below) |
| Palette | color, mono | mono: every color is a shade of the Colors hue (below) |
| Mode | terminal, CP/M machine | a serial terminal on UART0, or the console of the [Z80 CP/M machine](#z80-cpm-machine). Saving a new mode restarts the board |
| Status line | shown, hidden, debug, debug usb | hidden: row 24 stays blank (the text area is still 24 rows; the boot logo's build line and CP/M's drive lights are not shown). debug: video timing instead; debug usb: the USB host's last events (both below, refreshed four times a second) |
| Terminal type | VT100, VT52, ADM-3A | what the terminal is at power-on and after a reset: a VT100, a VT100 in VT52 mode, or an ADM-3A (80 columns only); see [Terminal types](#terminal-types). Changing to or from ADM-3A clears the screen |
| Blank after | never, 1, 5, 10, 30, 60 min | the screen goes black after this long without a key, against burn-in. The next key only wakes it (it is not sent). Output from the host or the Z80 does not wake it; it carries on unseen. The DVI signal is turned off, so the monitor can go into power saving (backlight off); on waking it has to resync, which takes it a few seconds |

**Debug status line**: `late T+N | line C/P% | F fps | loop M/Q ms |
render R/S us | Z MHz`: scanlines not ready in time for the DVI output (in
all, + since the last refresh); core 1's longest scanline as a share of
the time a scanline lasts (now / peak since debug came on); frames a
second; core 0's longest main-loop pass and longest single-line render
(now / peak); in CP/M mode, the Z80's speed. `late` should stay 0.

**Debug usb status line**: `1234s | usb: 1s led1 1s msc2 1s up2 10s vol2`:
the seconds since power-on (if they stop, the main loop has stopped:
keyboard, USB and CP/M), then the USB host's last four events, oldest
first, each as its time, the event and the device's USB address (`xN` for
repeats): `up`/`down` a device set up or gone, `kbd`/`kbd-` a keyboard,
`msc`/`msc-` a USB stick, `vol`/`vol?`/`unvol` the stick mounted as `USB:`,
not mountable, unmounted, `timeout`/`gone`/`ioerr`/`busy` a stick transfer
over 3 s, its stick pulled, failed, or refused, `led`/`led?` the keyboard
LEDs sent or refused (busy). The monitor's `USB:` line lists the devices.

**Colors**: green, amber or blue phosphor turns the 16 ANSI colors (default
text and bold included) into shades of that hue, by brightness (black; dark
gray; the normal colors; default text; the bright colors; bold); 256-color
and 24-bit colors stay in color. The status line, SET-UP's box and startup
logo match (white keeps the logo's own colors).

**Palette**: mono makes every color, 256-color and 24-bit too, a shade of
the Colors hue (white: gray) by its brightness, so colors of similar
brightness look alike. The status line becomes the text colors reversed
(and SET-UP's box the text color), and with white the startup logo shows in gray.

Under Palette, two rows preview both color settings: default text, bold and
the status line, then each of the 16 ANSI colors (0-F) as text on the
default background and, below it, as a background. While SET-UP is open the
whole screen shows the edited Colors and Palette; leaving without saving
puts the old ones back. Changing either recolors everything on screen, text
already there included: the screen holds palette entries, and only the
table that turns them into pixels changes.

It opens from:

- **F12** (or Pause) on a USB keyboard. While it is open, data from the
  host waits in the receive buffer (with RTS/CTS the host is held off).
- **`CSI ? 999 ~`** (`ESC [ ? 9 9 9 ~`) from the host, a sequence private to
  this terminal. The bytes the host sends next are taken as keys, so it can
  be driven from a PC: `tools/hostbridge.py` sends it on Ctrl-\ and then
  passes your keys to the terminal until Return or Esc.

| Key | Action |
|---|---|
| Up/Down | select a setting |
| Left/Right, Space | change it |
| **Return** | save to flash and apply (a new width clears the screen, as DECCOLM does) |
| **Esc**, F12 | leave without saving |
| **T** | show the demo screen in the edited colors until a key is pressed |
| **R** | reboot the board, as at power-on (unsaved changes are dropped; CP/M drives are written back first). The last resort when the terminal is stuck |
| **U** | [update the firmware](#updating-the-firmware-from-the-sd-card-or-a-usb-stick) from `FIRMWARE.UF2` on the SD card or a USB stick, in either mode |
| **D** | CP/M mode: the [Drives panel](#running-cpm) |
| **M** | CP/M mode: back to the [monitor](#the-monitor) |

The panel opens with the width, Backspace mode and LNM currently in use, so
Return alone makes a DECCOLM, DECBKM or LNM switch from the host the
power-on default. A new baud rate or framing applies as soon as it is
saved: the host has to be switched to match (opened from the host, SET-UP's
own keys then arrive at the new settings too). **T** does not keep the
terminal's screen (there is no memory for a copy), so SET-UP comes back
over a cleared screen, as after a width change.

Settings are kept across power loss and firmware updates; see
[Settings in flash](#settings-in-flash).

### Trying it from a PC

`tools/hostbridge.py` runs a program on this PC with the terminal as its
display: its output goes out over the serial adapter, and your keystrokes
here (and on a USB keyboard on the terminal) plus the terminal's replies
come back as its input. Ctrl-] quits; Ctrl-\ opens [SET-UP](#set-up).

```sh
tools/hostbridge.py /dev/ttyUSB0 vttest   # or: vim, top, $SHELL ...
tools/hostbridge.py --cols 132 /dev/ttyUSB0 $SHELL
# add --rtscts when the terminal is built with flow control
```

It sets `TERM=xterm-color` (8 colors); `--term vt100` turns color off,
`--term xterm-16color` adds bright colors. All the xterm entries ask for the
alternate screen, which the terminal ignores (see
[Limitations](#limitations)).

## Z80 CP/M machine

With **Mode** set to *CP/M machine* in SET-UP, the board is a small CP/M
computer: a Z80 emulated on core 0 (z80pack's CPU core, as in its
[picosim](https://github.com/udo-munk/z80pack/tree/master/picosim) for
the Pico) with the terminal as its console, the USB keyboard as its
keyboard, disk images on a microSD card, and UART0 as its second serial
port. Set Mode back to *terminal* for the plain serial terminal.

### SD card

Wire an SPI microSD adapter to SPI0 as in the [Hardware](#hardware) table.
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
(checked with cpmtools 2.23 on images written by the emulated CP/M).

Drive A must hold a bootable 8" floppy: CP/M reloads itself from its system
tracks on every warm boot. The built-in `CPM22` is a 62K CP/M 2.2 with a
BIOS for the eight drives and the three formats (`src/cpm`).

### Other operating systems

The machine has picosim's hardware, so the disk images for z80pack's
picosim run as they are, with their own BIOSes. They are not built in: copy
them from z80pack's
[`picosim/disks`](https://github.com/udo-munk/z80pack/tree/master/picosim/disks)
into `/DISKS80` and mount them from the monitor. Their BIOSes know only
four 8" floppy drives, A-D.

- **CP/M 3**: `cpm3-1.dsk` (boots) and `cpm3-2.dsk`. The banked system uses
  bank 1 only, so the default build runs it.
- **MP/M II**: `mpm-1.dsk` (boots) and `mpm-2.dsk`. Its `MPM.SYS` is
  generated for picosim's six user banks, but this machine has two
  (`MACHINE_BANKS`), and the machine stops (`MMU: no bank 3`) when MP/M
  selects a bank it does not have. Regenerate `MPM.SYS` for two banks with
  `GENSYS` from `mpm-2.dsk` first.

These images have not been tested on this board yet.

### WORMS

`CPM22` also carries `WORMS` (`src/cpm/worms.asm`): worms wandering about
the screen after BSD worms(6), each in its own color and glyph. It asks the
terminal for the screen size, so it fills 132 columns too. Any key ends it.

| Option | Effect |
|---|---|
| `-N n` | number of worms, 1-40 (default 3) |
| `-L n` | length, 2-255 (default 16) |
| `-D ms` | milliseconds per step (default 40; 0: as fast as the Z80 goes) |
| `-F` | a field of dots to eat |
| `-T` | a trail of dots |
| `-C` | 256-color rainbow worms: each head steps round a 30-color hue wheel from the xterm palette, so the bodies are gradients |
| `-M` | no color |

For example `WORMS -N 10 -L 30 -F` or `WORMS -C -N 6 -L 40`.

### TERMTEST

`Z80TESTS` carries `TERMTEST` (`src/cpm/termtest.asm`), a check of the
[terminal types](#terminal-types): run `B:TERMTEST` with `Z80TESTS` on B:,
pick the type the terminal is set to (for VT52, leave it on VT100:
TERMTEST switches it with `CSI ? 2 l` and back with `ESC <`), then any
key turns the page and Esc goes back to the menu. One page each for cursor
addressing, cursor moves (`OK` on every row when they are right), erasing,
inserting and deleting lines, scrolling, wrapping at the last column, line
graphics, the terminal's replies and the codes the keys send (Ctrl-C ends
that one). Each page says what it should look like. Install `Z80TESTS`
again from the monitor to get it onto a card that already has the disk.

### Moving files

`CPM22` carries three tools (`src/cpm/xls.asm`, `xpip.asm`, `xera.asm`)
that move files between CP/M disks and the folder `/XFER` on the SD card
(`SD:`) or on a USB stick (`USB:`): `XLS` lists the folder, `XPIP` copies,
`XERA` erases. Files go in and out without image tools on a PC. A
USB stick can be plugged in and pulled while CP/M runs, so nothing needs
switching off. They use the machine's file transfer device, port 10.

`XLS [SD:|USB:][afn]` lists `/XFER` with sizes in bytes:

```
A>XLS
README.TXT         4210
GAME.COM          18432
2 files
```

`XPIP dest=source [T]` copies, in the manner of `PIP`. One side is a CP/M
drive (`B:`, or none for the current drive), the other `SD:` or `USB:`:

| Command | Effect |
|---|---|
| `XPIP A:=SD:*.COM` | every `.COM` in `/XFER` to drive A, names kept |
| `XPIP SD:=B:*.TXT` | every `.TXT` on drive B to `/XFER` |
| `XPIP SD:NEW.TXT=OLD.TXT` | one file, renamed |
| `XPIP SD:LOG.TXT=B:LOG.TXT[T]` | a text file: cut at its end-of-file mark |
| `XPIP USB:=B:*.*` | everything on drive B to the USB stick |

- A file of the same name at the destination is replaced, as with `PIP`.
- CP/M files are whole 128-byte records. Copied to CP/M, a file's last
  record is padded with `^Z`; copied to `SD:`, every record is kept, and
  `[T]` cuts the file at the first `^Z` of its last record, as text
  editors on a PC expect.
- Names are 8.3: a PC's long names show as their short forms
  (`LONGFI~1.TXT`), and names CP/M cannot take (such as ones with `_`)
  are not listed.
- Any key stops a copy between files.

`XERA [SD:|USB:]afn` erases files in `/XFER` (`SD:` is the default),
naming each. With wildcards it says how many match and asks first: `Y`
erases them, anything else leaves them. `ERA` erases CP/M files.

**USB sticks** go into the USB port through a hub, next to the keyboard
(see [USB keyboard](#usb-keyboard) for powering the port). The stick must
be FAT12, FAT16 or FAT32 with 512-byte sectors: most sticks up to 32 GB
come that way; larger ones are usually exFAT and need formatting as FAT32
first. A card reader shows its first slot only. The status line shows
`USB` while a stick is `USB:`.

- **One stick at a time is `USB:`**: the first one plugged in. Another
  plugged in meanwhile waits (`USB+1` on the status line) and takes over
  when the first is pulled. A stick that cannot be used (not FAT, or
  exFAT) is passed over (`USB?`) until it is plugged in again.
- **Pull a stick between copies**, when `USB` is not lit: every file is
  closed when `XPIP` finishes with it. Pulled in the middle of a copy, the
  copy stops with an error and the file on the stick may be incomplete or
  missing (check the stick on a PC); the copy never carries on onto
  another stick.

`DIR` and `STAT` list the CP/M drives (z80pack's `XDIR`, which only took A
and B, is no longer on the disk).

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
  with its tools: boots), `Z80TESTS` (`TERMTEST`, and the Z80 instruction exercisers
  `EXZ80DOC` and `EXZ80ALL`, about 10 minutes each at full speed, and the
  `CPUTEST` diagnostics; `PRELIM` needs an emulator that runs block moves
  in one go, as z80pack's cpmsim does, and wrecks CP/M here: reset after
  it) and `EMPTY` (a blank disk). They are kept compressed in flash (about
  240 KB) to start a blank card with; existing files are only replaced if
  you say so.
- **S**: CPU speed test (a JP loop for a second, in Z80 MHz).
- **C**: check the card again (after changing it).
- **R**: resume the CPU, after leaving it from SET-UP.
- **F12**: SET-UP, also for the
  [firmware update](#updating-the-firmware-from-the-sd-card-or-a-usb-stick) (**U**).

### Running CP/M

Return sends CR alone, as CP/M expects, whatever the New line (LNM) setting
says (a following LF would wait as a key press, which makes DIR and TYPE
stop after their first line).

While CP/M runs, F12 opens SET-UP as usual (the CPU waits), with two more
keys:

- **D** opens the Drives panel, which shows what is mounted on A-H and
  changes it (Up/Down a drive, Left/Right or Space a disk image from the
  card, Return mounts and saves). CP/M sees a changed drive after a warm
  boot, so type **^C** at the prompt afterwards (as after swapping
  floppies; don't change a drive a program is using).
- **M** returns to the monitor.

**F11** is the reset button: it reboots from drive A at once (keys typed
ahead are dropped; with drive A empty it goes to the monitor). It resets the
terminal too, as RIS does (colours, modes, scrolling region, an unfinished
escape sequence or string), clears the screen and returns to the SET-UP
width. In terminal mode F11 sends its usual `ESC [ 2 3 ~`.

Two CP/M commands on the `CPM22` disk (`BYE.COM` and `RESET.COM`, from
z80pack) use the machine's hardware control port: `BYE` stops
the machine, and `RESET` reboots it.

### Emulated hardware

The machine is picosim's:

| Port | Device |
|---|---|
| 0/1 | console |
| 2/3 | second serial port (UART0 at the SET-UP baud rate) |
| 4 | disk controller |
| 5/6 | printer (none) |
| 8 | the format of the disk in a drive, for the BIOS (ours, not picosim's) |
| 10 | file transfer: files in the `/XFER` folder of the SD card or a USB stick, a 128-byte record at a time by DMA, for `XLS`, `XPIP` and `XERA` (ours; `src/machine.h` has the commands) |
| 64 | MMU: bank 0 plus `MACHINE_BANKS` 48 KB banks (default 2: enough for CP/M 3; MP/M II uses as many as it was generated for, see [Other operating systems](#other-operating-systems)) |
| 65/66 | clock (no RTC: it starts at 1 Jan 2026 at power-on) |
| 67 | 60 Hz timer interrupt |
| 160 | hardware control |

The CPU runs unthrottled in 10 ms slices between the keyboard, disk and
screen work: the monitor's speed test (a JP loop) measured 48.84 MHz (Z80
clock equivalent) on a Pico 2 at the 400 MHz DVI clock, and the status line
shows about 36 MHz while `EXZ80DOC` runs (its prefixed and block
instructions cost more per T-state).

### Updating the firmware from the SD card or a USB stick

Copy the new `dvi_terminal.uf2` to the root of the SD card or of a USB
stick as `FIRMWARE.UF2` (8.3 names only; the release ZIP has it as
`sdcard-<target>/FIRMWARE.UF2`, and `SDCARD=/media/<you>/<card>
scripts/build.sh` copies it there), put the card or stick in, and press
**U** in SET-UP (either mode; in terminal mode the card is mounted only for
the update). The stick is the one the status line shows as `USB`; with
`FIRMWARE.UF2` on both, **S** or **U** picks which to install. It reads the
file through and checks it (a UF2 for the RP2350 with a bootable image that
fits), shows its size and asks; **Y** then:

1. writes the image to the upper half of the flash, beside the running
   firmware, and reads it back (the progress bar). Any problem up to here
   leaves the board as it was;
2. renames the file to `FIRMWARE.OLD` (on the card or stick it came
   from), so it is not installed twice, and closes the disks;
3. copies the image over the running firmware (a few seconds; the screen
   says not to power off) and restarts with it. The settings are kept.

If the power fails during step 3 the board will not start: flash it over
USB with BOOTSEL as in `docs/FLASHING.txt`, which always works.

## Building

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

`scripts/build.sh` takes `TARGET` (a board header in `boards/`, default
`pico2-dvi-sock`; see [Other boards](#other-boards)), `SERIAL_FLOW_CONTROL`, `CMAKE_ARGS` (extra configure
arguments, e.g. `CMAKE_ARGS=-DVIDEO_DIAG=ON`) and `SDCARD` (a mounted card
to copy `FIRMWARE.UF2` to). It also writes `build/<target>/FIRMWARE.UF2`
and runs `scripts/check_fwupdate.py`, which fails the build if the firmware
update's final copy could read the flash it overwrites. Plain
`cmake -S . -B build/x` also works and picks up `.toolchain/`
automatically.

Toolchain versions are pinned in **`toolchain.env`**, the single source of
truth for `scripts/setup-toolchain.sh` and the CI cache key. To upgrade,
change the version and its SHA256 there. pico-sdk is upgraded by moving its
submodule.

### Build options

| CMake option | Default | |
|---|---|---|
| `SERIAL_FLOW_CONTROL` | OFF | RTS/CTS on the board's flow control pins, GP3/GP2 on the Pico 2 (see [Flow control](#flow-control)); `scripts/build.sh` takes it from the environment |
| `STARTUP_LOGO` | ON | show the startup logo at power-on/reset |
| `LOGO_IMAGE` | `assets/logo/ColorLogo-576.png` | the logo's PNG (see [Startup logo](#startup-logo)) |
| `LOGO_MS` | 10000 | how long the logo shows, in ms |
| `LOGO_IN_FLASH` | ON | experimental: keep the logo data in flash, copied out only while it shows (saves about 19 KB of SRAM); OFF copies it into SRAM with the program |
| `STARTUP_DEMO` | OFF | run the demo after the logo |
| `MACHINE_BANKS` | 2 | Z80 machine: 48 KB banks besides bank 0 (each costs 48 KB of SRAM; a third does not fit) |
| `HSTX_MODE` | 0 | 0: 800x600 at 60 Hz; 1: at 56 Hz (clk_hstx 180 MHz); 2: 60 Hz reduced blanking (177 MHz). 1 and 2 are slower fallbacks |
| `VIDEO_DIAG` | OFF | bring-up diagnostics on UART0 TX: boot markers, core 1 scanline timing, fault reports |
| `COPY_TO_RAM` | ON | run from SRAM; must stay ON (see [How it works](#how-it-works)) |

## How it works

Video comes from the RP2350's HSTX, whose TMDS encoder turns RGB565 pixels
into DVI (`src/hstx_dvi.c`, after Raspberry Pi's
`pico-examples/hstx/dvi_out_hstx_encoder`): 800x600 at 60 Hz, a 40 MHz pixel
clock, so clk_hstx 200 MHz and clk_sys 400 MHz (1.3 V). Two DMA channels
feed it blanking command lists and scanlines in turn. `hstx_test.uf2`,
built alongside, shows a test pattern for checking a monitor and the
clocks.

- **Core 0** runs the terminal: UART0 receive interrupt into a ring buffer
  (`src/serial.c`), the escape sequence parser (`src/vtparse.c`, Paul
  Williams' DEC state machine), the terminal (`src/term.c` with the
  personalities `src/vt100.c`, `vt52.c` and `adm3a.c`) on a cell
  buffer (`src/screen.c`), rasterization of changed lines (glyph bits per
  cell with the attributes applied, and each cell's colors:
  `src/raster.c`, driven by `video_update` in `src/video.c`), and the USB
  keyboard: TinyUSB host (`src/usbkbd.c`) with boot protocol reports turned
  into VT100 key codes (`src/keyboard.c`). In CP/M mode it also runs the
  Z80.
- **Core 1** composes each scanline from those into RGB565 pixels a couple
  of lines ahead of the DMA, inverts the cursor cell in the blink-on phase,
  and decodes the startup logo (`src/video.c`, `raster_compose`); the DMA
  interrupt runs on core 1 too.

The firmware runs entirely from SRAM (`COPY_TO_RAM`): the boot-time flash
(XIP) setup is chosen for clk_sys up to 150 MHz, far below the 400 MHz DVI
clock.

### Fonts and glyph codes

The screen buffer stores one-byte glyph codes (see `src/font.h`): ASCII at
0x20-0x7E, Latin-1 at 0xA0-0xFF, DEC Special Graphics (line drawing) at
0x01-0x1F, and shades/blocks/double-line box drawing at 0x80-0x9F. The
tables are generated at build time: Terminus 10x20 for 80 columns (bold
text in Terminus's bold face) and misc-fixed 6x12 at double height for 132
(no bold face fits 6 pixels, so bold text there is only brighter, as on a
VT100). They stay in flash; the current width's fonts are copied into
SRAM when the width changes. Glyphs a font lacks (control pictures,
scan lines, half blocks) are drawn by `font_gen.py`.

### Startup logo

`LOGO_IMAGE` is converted at build time by `src/logo_gen.py` (Python
standard library only). It must be a non-interlaced 8-bit PNG, at most
800x576 (576 is the 24 text rows; the status line stays below) and a
multiple of 8 pixels wide; alpha is composited over black, and it is shown
unscaled, centered.

The logo must be a single hue or gray on black: every color channel that
is not black must show the same levels (the default is green only). It is
posterized to 4 levels, with at most two levels in each 8-pixel group. The
encoder no longer needs that limit; it stays because the author likes the
look it gives. It is stored run-length encoded (about
19 KB for the default); while it shows, a copy sits in the text raster's
memory and core 1 decodes it a scanline at a time. The saved Colors setting
tints it: green, amber and blue show it in that hue (each of its four
levels gets a color), white shows it as it is.

### Settings in flash

Settings live in the two 4 KB sectors before the last one of the flash,
used in turn with a sequence number and a CRC, so a save cut short by a
power loss keeps the previous settings; flashing new firmware keeps them
too. (The last sector is erased by every UF2 flash: the SDK's RP2350-E10
workaround block for 0x10ffff00 wraps round to it.) A save holds up the
terminal for the erase (typically 30-50 ms); the picture stays up, as the
program runs from RAM. The flash is written with the 400 MHz DVI clock
running: the ROM's erase/program routines run its serial clock at
clk_sys / 12, and reads are set back to at most 50 MHz afterwards (see
`src/nvflash.c`).

### Resources used

As of v0.5.0 with the default build (`MACHINE_BANKS=2`):

| Resource | Used | Free |
|---|---|---|
| SRAM (520 KB) | about 475 KB: code 170 KB, data 20 KB, buffers 285 KB (Z80 memory 160 KB, raster memory 60 KB, the current fonts 20 KB, screen 13 KB, ...) | about 37 KB, plus most of the two 4 KB scratch banks. A third Z80 bank (48 KB) does not fit |
| Flash (4 MB) | firmware 496 KB; firmware-update staging area (upper 2 MB, used only during an update); settings (2 sectors); the last sector is left blank | about 1.5 MB below 2 MB for the firmware to grow into |
| Clocks | clk_sys 400 MHz at 1.3 V, HSTX 200 MHz, USB 48 MHz | |
| Peripherals | HSTX (DVI), 2 DMA channels, UART0, SPI0 (SD card), USB (host), TIMER0, watchdog | 14 DMA channels, all three PIO blocks, UART1, SPI1, I2C0/1, PWM, ADC, TIMER1, TRNG, SHA-256, the QMI's second chip select (PSRAM) |
| GPIO | GP0/1 UART0, GP2/3 RTS/CTS (flow control builds only), GP4-7 SD card, GP12-19 DVI Sock | GP8-11, GP20-22, GP26-28 (GP26-28 can be analog inputs), and GP2/3 without flow control |

## Project layout

| Path | Contents |
|---|---|
| `src/` | Firmware; see below |
| `boards/` | Board headers: pins, flash, crystal per build target; `example.h` is the template (see [Other boards](#other-boards)) |
| `test/` | Host unit tests (plain CMake + gcc with ASan/UBSan, no pico-sdk) |
| `tools/hostbridge.py` | Run a host program with the terminal as its display (see [Trying it from a PC](#trying-it-from-a-pc)) |
| `lib/pico-sdk` | Pinned submodule (plus its `lib/tinyusb`) |
| `lib/z80pack` | Vendored part of z80pack (Z80 core, z80asm, picosim disk images); `UPSTREAM.md` gives the commit and local changes |
| `lib/fatfs` | ChaN's FatFs R0.15a, vendored (see its README) |
| `assets/logo/` | The startup logo (PNG used by the build, JPEG original) |
| `assets/fonts/` | Terminus Font 10x20 and its bold face (SIL OFL 1.1) and misc-fixed 6x12 (public domain); see its README |
| `cmake/build_info.cmake` | Generates `term_build.h` (git describe, commit, date) on every build |
| `cmake/fonts.cmake` | Font table generation, shared by the firmware and host tests |
| `scripts/` | `setup-toolchain.sh`, `build.sh`, `test.sh`, `package.sh`, `changelog.sh` |
| `LICENSE` | The project's MIT license |
| `toolchain.env` | Pinned CMake, Arm GNU Toolchain and picotool versions |
| `docs/FLASHING.txt` | End-user flashing steps, shipped in the package |
| `docs/cpmtools-diskdefs` | cpmtools definition of the 8 MB disk format |
| `docs/THIRD-PARTY.md`, `docs/licenses/` | Third-party components and licenses (shipped, with the texts from the submodules, in `licenses/`) |

In `src/`:

- **Terminal, portable and host-tested**: `screen.[ch]` (cell buffer),
  `vtparse.[ch]` (escape sequence parser), `term.[ch]` (the terminal; its
  personalities `vt100.[ch]` with `vt52.c`, and `adm3a.[ch]`),
  `palette.[ch]` (the 256-color palette per Colors/Palette setting),
  `raster.[ch]` (cells to pixels), `keyboard.[ch]` (HID keys to VT100
  codes), `keyname.[ch]` (key names for the status line), `splash.[ch]`
  (startup logo decoding), `settings.[ch]` (settings records),
  `setup.[ch]` (SET-UP panel), `blank.[ch]` (screen blanking timer),
  `demo.[ch]`, `crc32.[ch]`, `uf2.[ch]` (UF2 file checks for the firmware
  update).
- **Z80 machine, portable**: `machine.[ch]` (z80pack glue; `z80pack/`
  holds its configuration headers), `monitor.[ch]`, `drives.[ch]` (disk
  images on FatFs), `drivepanel.[ch]` (SET-UP's Drives panel),
  `fwpanel.[ch]` (SET-UP's firmware update), `xfer.[ch]` (port 10's
  files: `/XFER`), `usbvol.[ch]` (which USB stick is `USB:`), `cpm/` (the
  CP/M 2.2 BIOS, boot loader, WORMS, XLS, XPIP, XERA and TERMTEST; see its README), `diskimg.[ch]` and `lz.[ch]` (the built-in
  images).
- **RP2350**: `main.c`, `board.h` (the board header's pins, checked),
  `hstx_dvi.[ch]` (HSTX DVI output), `video.[ch]`
  (scanlines on core 1), `hstx_test.c` (test pattern), `serial.[ch]` (UART
  with optional RTS/CTS and a TX queue), `nvflash.[ch]` (settings sectors,
  written at the DVI clock), `sdcard.[ch]` (SPI SD card for FatFs),
  `diskio.c` (FatFs drives: 0 the SD card, 1 a USB stick),
  `fwupdate.[ch]` (firmware update from the SD card), `usbkbd.[ch]`,
  `usbmsc.[ch]` (USB sticks), `tusb_config.h` (TinyUSB host) and
  `diag.[ch]` (`VIDEO_DIAG`).
- **Build-time generators**: `font_gen.py` (font tables), `logo_gen.py`
  (the logo), `diskimg_gen.py` (the compressed disk images).

## CI and releasing

- `test`: host unit tests (JUnit report).
- `build`: firmware per target (a matrix, today only `pico2-dvi-sock`);
  UF2/BIN/ELF/disassembly/map and `build-info.txt` artifacts.
- `package`: ZIP with UF2 and BIN per target, `FLASHING.txt`,
  `THIRD-PARTY.md` and `licenses/`, `CHANGES.md` (commits since the
  previous tag), `SHA256SUMS` and the source of the built commit. Automatic
  on the default branch and tags.
- `release`: tags only. Uploads the ZIP to the project's generic package
  registry (`dvi-terminal/<tag>`) and creates a GitLab Release linking to
  it, with `CHANGES.md` as the description.

To release, push an annotated tag:

```sh
git tag -a vX.Y.Z -m "dvi-feather-terminal X.Y.Z"
git push origin vX.Y.Z
```

Planned work and known issues are tracked with `bd` (beads): `bd ready`,
`bd list`.

## Licenses

This project's own code and documentation are under the MIT license; see
`LICENSE`. Files derived from other projects keep their original notices
(`src/hstx_dvi.c` from pico-examples; `src/machine.c`, `src/z80pack/` and
`src/cpm/` from z80pack's picosim, also MIT). The third-party parts:

Terminus Font is © Dimitar Toshkov Zhekov under the SIL Open Font License
1.1; see `assets/fonts/Terminus-OFL.txt`. The tables generated from it
are derivatives and stays under that license. The misc-fixed 6x12 font is
public domain. pico-sdk is BSD-3-Clause, as is the pico-examples HSTX DVI
example `src/hstx_dvi.c` follows.

TinyUSB (in pico-sdk) is MIT; pico-sdk's CMSIS headers are Apache-2.0.
z80pack is © Udo Munk and others under the MIT license (`lib/z80pack/LICENSE`);
`src/machine.c` and `src/z80pack/` follow its picosim, and its CPU core
includes `log.h` (Apache-2.0). FatFs is © ChaN under a 1-clause BSD license
(`lib/fatfs/LICENSE.txt`). The built-in disk images come from z80pack: CP/M
2.2 is Digital Research's, redistributed under the permission Lineo
(Caldera) gave in 2001 for the CP/M material of "The Unofficial CP/M Web
Site"; the Z80 exercisers are Frank D. Cringle's, GPL-2.0 or later (their sources are
on the same disk image).

[`docs/THIRD-PARTY.md`](docs/THIRD-PARTY.md) lists every third-party
component in the firmware with its license; release packages carry it and
the license texts in `licenses/`.
