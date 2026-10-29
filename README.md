# dvi-feather-terminal

A VT100 serial terminal with DVI output for the Raspberry Pi Pico 2
(RP2350) with the Pico DVI Sock: UART in, 1024x600 DVI out, 80 or 132
columns by 24 rows plus a status line, and a USB keyboard that types to the
host over the same UART. The same board can instead be a small Z80 CP/M
computer with the screen and keyboard as its console and its disks on a
microSD card (see [Z80 CP/M machine](#z80-cpm-machine)).

Releases (`vX.Y.Z` tags) ship the firmware; see
[Getting started](#getting-started). The user guide is online at
<https://the-next-bug.github.io/dvi-feather-terminal/> (the latest
release's) and in every release ZIP as `guide/`.

## Features

- **Display**: 1024x600p60 over DVI/HDMI (the 7" HDMI panels' native
  size, pixel for pixel; desktop monitors take it too), or 800x600p60,
  picked in SET-UP (Display). At 1024 wide, 80 columns use Terminus 12x24
  in 12-pixel cells (960 pixels, centered) and 132 columns misc-fixed 6x12
  at double height (each glyph row on two scanlines, like a VT100) in
  7-pixel cells (924 pixels, centered); at 800, Terminus 10x20 in 10-pixel
  cells across the full width and 6x12 in 6-pixel cells (792 pixels,
  centered). Rows are 24 pixels high; line drawing, block and shade
  characters join up between rows and across cells.
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
  nearest of those 256). xterm's alternate screen (`CSI ? 1049 h`, `?1047`,
  `?47`), so vim, less and htop put the screen back when they exit; see
  [ALTSCR](#altscr).
- **Keyboard**: a USB keyboard on the Pico 2's USB port; see
  [USB keyboard](#usb-keyboard).
- **Status line** (row 24): the terminal's state and the last key sent, for
  checking a keyboard without a host; in CP/M mode, the drives and the
  emulated clock rate. See [Status line](#status-line).
- **SET-UP**: power-on width, Backspace (DEL/BS), local echo, baud rate,
  data/parity/stop bits, new line mode, colors, palette, display mode
  (1024x600 or 800x600), mode and terminal type, kept in
  flash. F12 or Pause on the USB keyboard, or `CSI ? 999 ~` from the host,
  opens it; see [SET-UP](#set-up).
- **Startup logo**: at power-on/reset the logo shows for 10 seconds
  (monitors take a few to sync), with the build on the status line; the
  first byte on UART0 or a key press ends it early. See
  [Startup logo](#startup-logo).
- **Z80 CP/M machine** (SET-UP Mode): a built-in Z80 computer running CP/M
  2.2 or banked CP/M 3 (both built in) from a microSD card, on
  [z80pack](https://github.com/udo-munk/z80pack)'s CPU core. See
  [Z80 CP/M machine](#z80-cpm-machine).
- **Sound** (boards with an I2S DAC, such as the Frankless): BEL beeps,
  and the Z80 machine has an AY-3-8910 sound chip (emulated, in mono or
  stereo) with a test program, a tracker, a PSG tune player with three
  tunes and Forth words for it. See [Sound](#sound).
- **Demo**: a test screen of colors, fonts and line drawing that alternates
  between 80 and 132 columns every 5 seconds. **T** in SET-UP shows it
  until a key is pressed; with `-DSTARTUP_DEMO=ON` it also runs after the
  logo until the first byte arrives or a key is pressed.

### Limitations

- The alternate screen starts blank each time (xterm keeps what was last
  on it), and `?47` works as `?1047`. SET-UP opened over it at 132 columns
  has no room to save the rows under its panel: they come back blank (the
  program redraws them on Ctrl-L, in most).
- Double-width and double-height lines show as normal lines; scrolling is
  jump scrolling only (DECSCLM is ignored).
- OSC, DCS, SOS, PM and APC strings (window titles, sixels, ...) are
  discarded. A string that is never terminated (an Alt+] typed by
  accident sends `ESC ]`) ends at the next control character, which then
  acts as usual, or after 1024 bytes, as with xterm's brokenStringTerm.

## Hardware

A Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock for the DVI/HDMI
connector. Everything else is optional:

| Pico 2 pin | Signal | Connects to | Needed for |
|---|---|---|---|
| GP0 (pin 1) | UART0 TX, data out | serial adapter RX | terminal |
| GP1 (pin 2) | UART0 RX, data in | serial adapter TX | terminal |
| GND | ground | serial adapter GND | terminal |
| GP3 (pin 5) | RTS out: terminal ready to receive | serial adapter CTS | [RTS/CTS flow control](#flow-control) only |
| GP2 (pin 4) | CTS in: host ready to receive | serial adapter RTS | [RTS/CTS flow control](#flow-control) only |
| GP4 (pin 6) | SPI0 MISO | SD card DO | [CP/M machine](#z80-cpm-machine) |
| GP5 (pin 7) | SD card CS | SD card CS | CP/M machine |
| GP6 (pin 9) | SPI0 SCK | SD card CLK | CP/M machine |
| GP7 (pin 10) | SPI0 MOSI | SD card DI | CP/M machine |
| 3V3 (pin 36), GND | power | SD card VCC, GND | CP/M machine |
| VBUS (pin 40) | 5 V in | 5 V supply | [USB keyboard](#usb-keyboard), unless a powered USB OTG adapter powers it |
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
  RTS/CTS flow control (without them SET-UP offers only XON/XOFF).
- **SD card**: which SPI, its MISO/SCK/MOSI, and CS (any GPIO).
- **USB host**: an optional pin that switches on the port's 5 V.
- **LED** (optional): off while the screen is on, flashing while the SD
  card, a USB stick or the serial line is busy. With the screen blanked it
  also flashes once a second, so a board that has hung shows a light that
  stays on or stays off.
- For a board without a Pico 2 module, the RP2350 variant, flash size and
  crystal (a pico-sdk board header; the SDK's own boards can be included,
  as the DVI Sock's does).

Board headers so far (the DVI Sock and the Feather with PSRAM and the
Adalogger FeatherWing are checked on hardware, the Frankless in part):

| Target | Board |
|---|---|
| `pico2-dvi-sock` | Pico 2 + Pico DVI Sock (the default) |
| `frankless` | [Frankless](https://github.com/arlaneenalra/pi-pico-boards/tree/main/reworked/frankless), a reworked MicroFRANK (a board of the [FRANK platform](https://frank.rh1.tech/)): UART1 on J8 with CTS/RTS, the Z80 machine's banks in its 8 MB PSRAM (`PSRAM_BANKS`), DS3231M real-time clock, TDA1387 DAC for the bell and the sound chip (U8, hand-fitted) |
| `feather-rp2350-hstx` | Adafruit Feather RP2350 with HSTX Port + DVI breakout; SD on SPI0 with CS on D10 (Adalogger FeatherWing); clk_sys 336 MHz (`BOARD_SYS_KHZ`), as it crashes at 400 MHz |
| `feather-rp2350-hstx-psram` | The same Feather with its 8 MB PSRAM fitted: the Z80 machine's banks in the PSRAM (15 of 48 KB) instead of SRAM (`PSRAM_BANKS`) |
| `feather-rp2350-hstx-adalogger` | `feather-rp2350-hstx` with the Adalogger FeatherWing's PCF8523 real-time clock on I2C1 (SDA/SCL, GP2/3); no RTS/CTS (those were GP2/3) |
| `feather-rp2350-hstx-psram-adalogger` | `feather-rp2350-hstx-psram` with the Adalogger FeatherWing's PCF8523, as above |

Experimental headers for boards that are not here, from their schematics
and never run on one, are in `boards/experimental/` (the stock MicroFRANK,
`microfrank`, and the 7West PicoTop Rev2, `picotop-rev2`). CI does not
build them and releases leave them out; `EXPERIMENTAL=1
TARGET=<target> scripts/build.sh` builds one
([boards/experimental/README.md](boards/experimental/README.md)).

All four Feather targets run at 336 MHz instead of the other boards' 384:
on two Feathers the firmware crashed at random at 400 MHz, with or without
the PSRAM. The video is the same, 1024x600 or 800x600; the CP/M machine
runs about 12% slower.

`src/board/board.h` checks the header when it compiles. A missing setting, a pin
that cannot carry its signal, or a pin used twice stops the build with an
error that names the setting (`test/test_board.sh` covers the checks). The
messages that show pins on screen use the header's pins.

### Flow control

SET-UP's **Flow control** picks it: `none` (the default), `RTS/CTS` or
`XON/XOFF`, applied when SET-UP saves, in both modes (in CP/M mode it
covers the Z80's second serial port). Without it, at 115200 baud the
terminal keeps up with ordinary output, but a long burst that scrolls the
whole screen can overrun it (the 2 KB receive buffer fills and characters
go missing); nor does anything stop the host while SET-UP is open.

**RTS/CTS** (hardware):

1. Wire the board's CTS and RTS pins (GP2 and GP3 on the Pico 2, as in the
   [table above](#hardware); the board header names them) to the
   adapter's RTS and CTS.
2. Set Flow control to `RTS/CTS` and save.
3. Enable RTS/CTS on the host side too: `stty -F /dev/ttyUSB0 crtscts`, or
   `tools/hostbridge.py --rtscts ...`.

The terminal drops RTS when its receive buffer is nearly full and raises
it again once half of it is free; it sends only while CTS is asserted. Do
not choose it without the wires: an unconnected CTS only floats on the
pin's weak pull, and if it reads as "hold" the terminal never sends its
keys or replies (device attribute and cursor position reports) to the
host. Without RTS/CTS the two pins are left unused. Boards without the
pins (the Adalogger Feathers: their RTC has GP2/3) do not offer it.

**XON/XOFF** (software, no wires): the terminal sends XOFF (`^S`) when
its receive buffer is half full (and again every 256 bytes if the host
keeps sending) and XON (`^Q`) once it has drained to 256 bytes; it stops
sending when the host sends XOFF and goes on at XON. Those two bytes are
flow control both ways, so they never reach the screen or the Z80, and a
program that needs them as data (Emacs and WordStar keys, XMODEM) needs
another setting. On the host: `stty -F /dev/ttyUSB0 ixon ixoff`, or
`tools/hostbridge.py --xonxoff ...`.

The status line shows `RTS/CTS` or `XON/XOFF` after the line settings.

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
or cable; a hub (or a keyboard with one built in) works too, but only one:
the firmware has room for a single hub (the Frankless's built-in one
counts), and with a second hub behind it the USB host may misbehave (new
devices no longer found, those already connected still working). The firmware is
the USB host, so the port is no longer the board's power input, and the
board does not switch 5 V onto the connector's VBUS by itself: feed 5 V into
**VBUS** (pin 40), which is the micro-USB connector's VBUS, or use a
**powered USB OTG adapter** (one with its own power input), whose supply
powers the keyboard and the board. The Feathers are the same: a powered OTG
adapter on their USB-C port, or 5 V into their **USB** pin.

Never connect a PC (or charger) to the USB port while 5 V is fed in either
way: both would drive VBUS. To flash, disconnect the 5 V supply (or the
powered adapter) and the keyboard, then use the USB port as usual (BOOTSEL).

Keys (US layout) send what a VT100 sends: letters honor Caps Lock, Ctrl
makes control characters, Alt sends ESC first (meta). Return sends CR (CR LF
in LNM), Backspace BS and Ctrl-Backspace DEL, or the other way round with
DECBKM reset (`CSI ? 67 l`; its power-on value is a SET-UP setting, BS by
default). The cursor
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
the baud rate and framing (`115200 8N1`, plus `RTS/CTS` or `XON/XOFF`
with [flow control](#flow-control)), `kbd` while a keyboard is attached, the USB stick (as in CP/M
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
| Columns | 80, 132 | the width at power-on and after F11 in CP/M mode. With Terminal type ADM-3A it shows `80 (ADM-3A)` and is skipped; the choice comes back with VT100 or VT52 |
| Backspace sends | DEL, BS | DECBKM at power-on and after RIS; default BS (vttest and CP/M software want it) |
| Local echo | off, on | keys typed are shown on the terminal as well as sent |
| Baud rate | 300 - 115200 | default 115200 |
| Data/parity/stop | 8N1, 8E1, 8O1, 8N2, 7E1, 7O1, 7N1, 7N2 | default 8N1 |
| Flow control | none, RTS/CTS, XON/XOFF | default none; RTS/CTS only on boards with the pins; see [Flow control](#flow-control) |
| New line (LNM) | off, on | LNM at power-on and after RIS: Return sends CR LF, and LF also returns the cursor. Not used in CP/M mode |
| Colors | white, green, amber, blue | white: the 16 ANSI colors as on a VGA screen; the others: phosphor shades (below) |
| Palette | color, mono | mono: every color is a shade of the Colors hue (below) |
| Display | 1024x600, 800x600 | the DVI mode, both at 60 Hz (default 1024x600, or the build's `DVI_MODE`). Saving a new one restarts the board in it, which then asks for a key: without one within 15 seconds (say the monitor shows nothing) it goes back to the old mode and restarts again. Until a key keeps it, every start asks, so switching the board off and on during the wait also comes back to the old mode |
| Mode | terminal, CP/M machine | a serial terminal on UART0, or the console of the [Z80 CP/M machine](#z80-cpm-machine). Saving a new mode restarts the board |
| Status line | shown, hidden, debug, debug usb | hidden: row 24 stays blank (the text area is still 24 rows; the boot logo's build line and CP/M's drive lights are not shown). debug: video timing instead; debug usb: the USB host's last events (both below, refreshed four times a second) |
| Terminal type | VT100, VT52, ADM-3A | what the terminal is at power-on and after a reset: a VT100, a VT100 in VT52 mode, or an ADM-3A (80 columns only); see [Terminal types](#terminal-types). Changing to or from ADM-3A clears the screen |
| Blank after | never, 1, 5, 10, 30, 60 min | the screen goes black after this long without a key, against burn-in. The next key only wakes it (it is not sent). Output from the host or the Z80 does not wake it; it carries on unseen. The DVI signal is turned off, so the monitor can go into power saving (backlight off); on waking it has to resync, which takes it a few seconds |
| USB log | off, SD card USBLOG.TXT | keep a log of the USB host in `USBLOG.TXT` on the SD card (default off); see "USB log on the SD card" below |

**Debug status line**: `late T+N|line C/P%|Ffps|loop M/Qms|render
R/Sus|Z MHz|snd A%`: scanlines not ready in time for the DVI output (in
all, + since the last refresh); core 1's longest scanline as a share of
the time a scanline lasts (now / peak since debug came on); frames a
second; core 0's longest main-loop pass and longest single-line render
(now / peak); in CP/M mode, the Z80's speed; on a board with a DAC, the
share of core 0's time the sound takes (mixing the bell and the sound
chip). `late` should stay 0.

**Debug usb status line**: `1234s | usb: 1s led1 1s msc2 1s up2 10s vol2`:
the seconds since power-on (if they stop, the main loop has stopped:
keyboard, USB and CP/M), then the USB host's last four events, oldest
first, each as its time, the event and the device's USB address (`xN` for
repeats): `up`/`down` a device set up or gone, `kbd`/`kbd-` a keyboard,
`msc`/`msc-` a USB stick, `vol`/`vol?`/`unvol` the stick mounted as `USB:`,
not mountable, unmounted, `timeout`/`gone`/`ioerr`/`busy` a stick transfer
over 3 s, its stick pulled, failed, or refused, `led`/`led?` the keyboard
LEDs sent or refused (busy), `stuck`/`probe?`/`probe!`/`nprobe` the USB
host restarted because it looked stuck, `mscrst` restarted because a USB
stick stopped answering (`timeout`/`badcsw` before it), `rstwait` a restart
waiting because the last one was too recent (below). The monitor's `USB:`
line lists the devices, and how often the host was restarted.

**USB host restarts**: the firmware checks the USB host by itself. When a
device has been setting up for over 20 seconds, the hub (or the device on
the port) stops answering a status request sent every 5 seconds, or a USB
stick stops answering in the middle of a command, it restarts the USB host.
Every device is dropped and set up again, as at power-on, without resetting
the terminal or the Z80 machine; the keyboard is gone for about half a
second. A USB stick in use as `USB:` is unmounted, as when it is pulled,
and mounted again. If restarting does not help, it waits longer each time
before the next restart (up to 16 times as long).

**Crash reports**: if the firmware stops (a crash inside the firmware, or
the main loop stuck for 8 seconds: no clock, no keyboard), the status line
says so: `PANIC ...`, `HARD FAULT ...` or `STUCK ...` with the code
addresses involved, which identify the place with the build's
`dvi_terminal.elf`. After a crash, or 20 seconds stuck inside an interrupt
handler, the board restarts by itself after 20 seconds; the next start
shows `The last run ended with: ...` under the banner and adds it to the
USB log. `STUCK` also shows during long operations that keep the main loop
busy, such as formatting a card or checking a firmware file; it goes away
once they finish.

**USB log on the SD card**: with SET-UP's **USB log** set to `SD card
USBLOG.TXT` (and saved), the USB host keeps a log in `USBLOG.TXT` in the SD
card's root, created if it is not there. Every few seconds the events
above, TinyUSB's own error messages and any restart (with the devices it
saw) are added to the end of the file, each line with the seconds since
power-on. Each power-on starts with a line giving the firmware and the
time. Set it back to `off` to stop the log (the file stays). In terminal
mode the card is mounted for the log; in CP/M mode it is written only while
the monitor has the card mounted.

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
  host waits in the receive buffer (with flow control the host is held off).
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
| **C** | the Clock and sound panel: set the date and time, choose how the status line shows them ([The clock](#the-clock)); the Bell, the sound chip's level and stereo, whether the DAC is fitted ([Sound](#sound)) |
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
# add --rtscts or --xonxoff to match the terminal's Flow control
```

It sets `TERM=xterm-color` (8 colors); `--term vt100` turns color off,
`--term xterm-16color` adds bright colors. All the xterm entries use the
alternate screen.

## Z80 CP/M machine

With **Mode** set to *CP/M machine* in SET-UP, the board is a small CP/M
computer: a Z80 emulated on core 0 (z80pack's CPU core, as in its
[picosim](https://github.com/udo-munk/z80pack/tree/master/picosim) for
the Pico) with the terminal as its console, the USB keyboard as its
keyboard, disk images on a microSD card, and UART0 as its second serial
port. Set Mode back to *terminal* for the plain serial terminal.

Digital Research's own sources for CP/M 2.2 and CP/M 3 (CP/M Plus), its
tools (MAC, RMAC, LINK, GENCPM) and much else are on
[The Unofficial CP/M Web Site](http://www.cpm.z80.de/) (its
[source page](http://www.cpm.z80.de/source.html); the site's HTTPS
certificate has lapsed, so use http). The disks here are built from
z80pack's binaries for now.

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
BIOS for the eight drives and the three formats (`src/machine/cpm`).

### Other operating systems

The machine has picosim's hardware, so the disk images for z80pack's
picosim run as they are, with their own BIOSes. Those BIOSes know only
four 8" floppy drives, A-D, and never ask the machine what a drive holds,
so the disk controller refuses them 4 MB and 8 MB images (a disk error
instead of reading a hard disk as a floppy and writing over it).

- **CP/M 3** (built in): mount `CPM31` on A: and `CPM32` on B: in the
  monitor and boot (Return); mount `CPM22` on A: again to go back. The
  banked system uses bank 1, so the default build runs it. Its BIOS is
  ours (`src/machine/cpm/cpm3`): eight drives, A-H, each any of the image
  formats, as under CP/M 2.2, with a 61K TPA. Mounting another image on a
  drive while CP/M 3 runs needs no `^C`: the BIOS tells the BDOS the
  media changed (port 8 gives each drive's format and a count of the
  images mounted on it). `CPM32` is an 8 MB disk (picosim's second
  floppy's files, with room to spare). The BIOS sets `SETDEF`'s defaults
  at every cold boot: programs are found on the default drive, then A:,
  then B:, so `XLS`, `XPIP`, `XERA`, `MEM`, `HWINFO`, `AYTEST`, `AYPLAY`, `AYTRACK` and `WORMS` on `CPM32` run
  from any drive; COM files before SUB files; temporary files on B:, not
  the nearly full A:. `README.TXT` on `CPM31` describes them and how to
  change them with a `SETDEF` line in `PROFILE.SUB`. CP/M 3 shows the
  user number in its prompt (`3A>`),
  finds system files in user 0 from any user area, and keeps the date
  (`DATE`). `CPM3GEN` holds the BIOS sources and `GENCPM.DAT`, `GENCPM`,
  and the RELs, SPRs and `CPM3.SYS` the firmware's build made from them
  (`SYSGEN.SUB` lists the steps; `RMAC` and `LINK` are on `CPM32`).
- **MP/M II** (copy `mpm-1.dsk` and `mpm-2.dsk` from z80pack's
  [`picosim/disks`](https://github.com/udo-munk/z80pack/tree/master/picosim/disks)
  into `/DISKS80`): `mpm-1.dsk` (boots) and `mpm-2.dsk`. Its `MPM.SYS` is
  generated for picosim's six user banks, but this machine has two
  (`MACHINE_BANKS`), and the machine stops (`MMU: no bank 3`) when MP/M
  selects a bank it does not have. Regenerate `MPM.SYS` for two banks with
  `GENSYS` from `mpm-2.dsk` first. The `feather-rp2350-hstx-psram` build
  has 15 banks, enough for picosim's `MPM.SYS` as it is (not tried yet).

MP/M II has not been tested on this board yet.

### Forth (F83)

The `F83` disk (built in, 8 MB) holds Henry Laxen and Michael Perry's
F83, FORTH-83 2.0 for CP/M-80 (public domain, `lib/f83`): `F83.COM`
ready to run, the system's source as block files (`KERNEL80.BLK`,
`META80.BLK`, `CPU8080.BLK`, `EXTEND80.BLK`, `UTILITY.BLK`), examples
(`CLOCK.BLK`, `HUFFMAN.BLK`, `BASIC.BLK`, a BASIC compiler) and their
manual, `README.80`. Mount it on B: under CP/M 2.2 or 3 and run it,
with a block file to open if you like:

```
A>B:
B>F83 CLOCK.BLK
8080 Forth 83 Model
Version 2.1.0 Modified 01Jun84
2 3 + . 5  ok
1 LIST
BYE
```

`README.80` is a WordStar file (high bits set), so `TYPE` shows it
garbled; `-READ.ME` and `F83-FIXS.TXT` are plain text.

`VIEW name` (and the editor's `FIX`) lists the source screen of a word.
The original F83 opens the source files on the current drive, so under
CP/M 3, where `F83` at `A>` is found on B: through the search path, it
fails with `Open error`. The disk's `F83.COM` is patched by
`VIEWDRV.BLK` (`src/machine/cpm/f83/viewdrv.fth`, loaded into the
original at build time) to look on the drive it was loaded from, which
CP/M 3 records at 0050H. CP/M 2.2 does not record it, so there
start F83 from its own drive (`B:`, then `F83`), as above. After
rebuilding F83 from its source (`README.80`), `F83 VIEWDRV.BLK` and then
`1 LOAD` patch and save the new `F83.COM` the same way.

`AY.BLK` (`src/machine/cpm/f83/ay.fth`) has words for the
[sound chip](#sound): `F83 AY.BLK`, then `1 LOAD`, then for example
`SCALE`, `CHORDS`, `SIREN`, `DRUM`, `PAN` (A, B and C in each stereo
layout) or `TUNE`. The words: `AY!` `AY@` `AY?` `TONE` `VOLUME` `NOISE`
`VOICES` `ENVELOPE` `QUIET` `STEREO`, `HZ>PERIOD` `NOTE>PERIOD` `NOTE`,
`TICKS` `WAIT`. Each screen's shadow (its number + 7) explains it, and
`VIEW` shows a word's source.

### The clock

The machine's clock (ports 65/66) is the board's real-time clock when it
has one (the Frankless's DS3231M, with a cell in its battery holder;
the Adalogger FeatherWing's PCF8523, with a CR1220 in its holder),
read at power-on and kept by the firmware from there, a second at a time
on the chip's 1 Hz output (GP8 on the Frankless) so it keeps the chip's
time instead of drifting. The Adalogger's 1 Hz output is not wired to the
Feather, so there the firmware's timer keeps it until the next power-on.
The PCF8523 is the less exact chip (about 2 seconds a day off, either
way) and keeps the years to 2099. Without an RTC, or while its time is
not set, the clock starts at 1 Jan 2026 at power-on.

Once the time is real (read from the RTC, or set since power-on), both
modes' status lines end in it, `13:45` by default, the colons blinking: on
for the first half of each second, in step with the RTC's 1 Hz output
where it has one.

SET-UP's **C** opens the Clock and sound panel, in either mode (its sound
rows: [Sound](#sound)):

- **Date** and **Time** show the clock as it runs. Typing digits over
  them (Left/Right move the cursor, Backspace goes back) and Return sets
  the clock to them, and the RTC with it. This is the way to set it under
  CP/M 2.2 and in terminal mode; under CP/M 3, `DATE SET` works too.
- **Status line clock** (24 h, 12 h, off), **Seconds** and **Date**
  (off, `2026-10-22`, `Thu 22 Oct`) choose what the status line shows; a
  row underneath shows the result. Return takes them back to SET-UP, whose
  Return saves them with the other settings. At its widest
  (`Thu 22 Oct  1:45:30 PM`) the clock takes 24 columns, and the end of the
  terminal's status line (the last key) gives way to it.
- Esc goes back to SET-UP without changes. Without an RTC the panel says
  the time lasts only until power-off.
Under CP/M 3, `DATE` shows it and `DATE SET` sets it, the RTC's too, so it
survives power-off; `HWINFO` says which RTC it is, whether it answered,
whether its time was set, whether its 1 Hz output ticks and, for the
PCF8523, whether its battery is low. Files written on the SD card and USB sticks (`XPIP SD:...`,
`DRIVES.CFG`, new disk images) get their dates from it.

### Sound

On a board with a DAC (`BOARD_AUDIO_I2S_*` in its header: the Frankless's
TDA1387), the firmware plays a continuous 32 kHz stereo stream to it. Other
boards stay silent, and their sound settings show `off (no DAC on this
board)`.

**The bell**: BEL from the host or from a CP/M program sounds a short
beep: 100 ms of 1 kHz, a new BEL while one sounds adding nothing. Ctrl-G at
a CP/M prompt is no test: CP/M echoes it as `^G`; F83's `7 EMIT` or a
host's `printf '\a'` sends a real BEL.

**The sound chip**: the Z80 machine has a General Instrument AY-3-8910
(three square-wave tones, noise and an envelope; emulated, clocked at
1.8432 MHz) on RC2014's ports: `OUT (D8h)` selects a register, `OUT
(D0h)` writes it, `IN (D0h)` reads it. A tone of f Hz has period 115200 /
f. Programs written for RC2014's YM2149 sound board use those ports.

The sound rows of SET-UP's Clock and sound panel (**C**):

| Setting | Values | |
|---|---|---|
| Bell | off, 1 - 10 | how loud BEL beeps, 2 dB a step (default 3, safe in headphones); a beep sounds at each new level |
| Sound chip | off, 1 - 10 | the AY's level, 2 dB a step (default 7; 10: three channels at full volume just reach the DAC's full scale); a chime sounds at each new level |
| Stereo | mono, ABC, ACB | where the AY's channels are heard: all in both ears, or A left, B center, C right (ABC, as on the ZX Spectrum 128 and Amstrad CPC), or A left, C center, B right. Mono by default: a channel in one ear only tires on headphones |
| Sound DAC | fitted, not fitted | not fitted: no sound at all (the DAC's pins, PIO and DMA are given back), and the AY's ports read FFh as with no chip |

A program can pick the layout itself through register 16, which the
firmware adds (a real AY ignores it): write 0 for the Stereo setting's, 1
mono, 2 ABC, 3 ACB. Reading it gives that in bits 0-1 and the layout in
use (1-3) in bits 4-5; FFh means older firmware or a real chip, so no
stereo. A machine reset (F11) puts it back to 0.

`CPM22`, `CPM31` and `CPM32` carry `AYTEST` (`src/machine/cpm/aytest.asm`),
a menu of the chip's parts: a scale, chords on A, B and C, a noise sweep,
the 16 envelope shapes, a volume ramp, A, B and C in turn (to hear the
stereo layout), or all of them; a key stops a part, `Q`, `Esc` or `^C`
the program. With the stereo register, **S** steps the layout (the
Stereo setting's, mono, ABC, ACB); the one AYTEST found is put back when
it ends.

`CPM22` and `CPM32` carry `AYPLAY` (`src/machine/cpm/ayplay.asm`), which
plays PSG files, the AY's register writes frame by frame (as emulators
record them):

```
A>AYPLAY JACQUES ABC
Playing JACQUES.PSG in ABC stereo (Q stops)
  0:07
```

`AYPLAY` alone lists the `.PSG` files on the drive. After the name:
`MONO`, `ABC` or `ACB` for the layout (else the Stereo setting's), `LOOP`
to play it until stopped. `Q`, `Esc` or `^C` stops it. Three tunes come
with it, our arrangements of public-domain melodies (made at build time
by `src/machine/cpm/tunes/psg_gen.py`): `KOROBEIN` (Korobeiniki, the
Tetris theme), `JACQUES` (Frere Jacques as a round in three voices: in
ABC it moves from left to right) and `ODE` (Ode to Joy). PSG files from
elsewhere play too, copied in with `XPIP`.

`CPM22` and `CPM32` also carry `AYTRACK` (`src/machine/cpm/aytrack.asm`),
a tracker to write tunes with: `AYTRACK [NAME]` (default `SONG`) edits
`NAME.AYT`, a song of up to 16 patterns of 32 rows, each row a note and an
instrument on channels A, B and C, played in the order list's order (up to
64), with 8 instruments (16 volume steps, one a frame, the last held; tone,
noise or both; a noise period) and a speed (frames a row, at 50 a second).
The screen shows the pattern, the order list and the instruments; Tab moves
between them:

- **Pattern**: the arrows move; the keys `Z S X D C V G B H N J M` play
  and enter a note in the octave shown (`[` `]` change it), `Q 2 W 3 E R 5
  T 6 Y 7 U` the next octave up (the table under the instruments shows
  which key plays which note), with the instrument shown; `=` is a note
  off, `.` or Delete clears; in the instrument column `0`-`7`. `^N` and
  `^P` show the next or previous pattern.
- **Orders**: Up/Down select (and show) an entry, `0`-`F` sets its
  pattern, `I` inserts a copy after it, `X` deletes it.
- **Instruments**: the arrows select an instrument and a field, `0`-`F`
  sets a volume (then the next), the mode (`1` tone, `2` noise, `3` both)
  or the noise period (two digits); the instrument is heard on each
  change.
- Anywhere: Space plays the song from the selected order, Return the
  pattern on screen, both looping while you edit (Space or Return again
  stops); `<` `>` the speed; `^S` saves `NAME.AYT`, `^E` writes the song
  once through as `NAME.PSG` for `AYPLAY`; `^Q` quits (unsaved changes are
  lost).

In [Forth](#forth-f83), `AY.BLK` on the `F83` disk has words for the
chip and examples.

### HWINFO

`CPM22`, `CPM31` and `CPM32` carry `HWINFO` (`src/machine/cpm/hwinfo.asm`):
the firmware, the board, and a line for each piece of hardware the board
has: whether it is in use (`yes`; `no` when nothing is plugged in or the
build does not use it; `missing` when the board should have it but it did
not answer; `planned` when the board header names it but the firmware does
not drive it yet) and what it is, a screen at a time (`-- More --`: any
key for the next page, `Q`, `Esc` or `^C` to stop). What a board does not
have is left out: its header (`boards/<target>.h`, `boards/example.h`)
says what it has. On a Pico 2 with the DVI Sock:

```
Firmware         DVI Feather Terminal v0.9.0 (2026-10-15)
Board            pico2-dvi-sock

CPU              yes      RP2350A at 400 MHz, 4 MB flash, quad reads at 50 MHz
Z80 memory       yes      64 KB + 2 x 48 KB banks in SRAM
Video            yes      DVI 800x600 at 60 Hz, 80 x 24 text + status line
Serial port      yes      UART0 TX GP0 / RX GP1, 115200 8N1 (SIO2)
Flow control     no       off (SET-UP: RTS/CTS or XON/XOFF)
SD card          yes      SDHC/SDXC, 29.7 GB; SPI0: GP4 MISO, GP5 CS, GP6 SCK,
                          GP7 MOSI
USB host         yes      native USB port, 1 keyboard
USB stick        no       none
Status LED       yes      GP25: disks, serial, heartbeat
Disk drives      yes      A: CPM22, B: Z80TESTS
```

It reads them from the hardware information device on port 12, which
programs can use too (the protocol is in `src/machine/machine.h`): `OUT`
an item number, then `IN` its bytes, and 0 after its end. Item 0 reads
`H`, `W` and the protocol version; on older firmware or z80pack's picosim,
port 12 reads `0FFH` and `HWINFO` says there is no information. Items 1
and 2 are the firmware and the board as text; from 16 on, each item is one
piece of hardware at a fixed number (16 the CPU, 18 the PSRAM, 26 the
real-time clock, ...): a status byte (1 yes, 2 no, 3 missing, 4 planned;
0 no such item, or none on this board), then its name and its
description, each ending in 0.

### MEM

`CPM22` and `CPM32` carry `MEM` (`src/machine/cpm/mem.asm`): the CP/M version,
the TPA (the room programs get, up to the BDOS or an RSX below it), the
machine's memory from the MMU, and what each bank holds:

```
CP/M 3.1
TPA     0100H-F205H: 61702 bytes (60K)
Memory  160K: bank 0 64K, banks 1-2 48K each (C000H-FFFFH common)
Bank 0  the system: banked BDOS and BIOS, disk buffers
Bank 1  the TPA (programs)
Bank 2  not used by CP/M
```

`MEM T` then tests the banks CP/M does not use (all but bank 0 and the
TPA's; not under MP/M), for instance the PSRAM of the
`feather-rp2350-hstx-psram` build: every free bank is filled with a
pattern made from the address and the bank number, then each is read
back, and again with the complement, so every bit is written both ways
and two banks that land on the same memory show up as errors. It changes
those banks' contents, and runs with interrupts off while another bank is
selected (from C000H, in common memory). Under CP/M 2.2 on that build:

```
Testing the free banks (two passes)...
Bank 1  ok
...
Bank 15  ok
```

A bank with errors shows how many bytes were wrong (`Bank 3  12 bytes
wrong`).

### WORMS

`CPM22` also carries `WORMS` (`src/machine/cpm/worms.asm`): worms wandering about
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

`Z80TESTS` carries `TERMTEST` (`src/machine/cpm/termtest.asm`), a check of the
[terminal types](#terminal-types): run `B:TERMTEST` with `Z80TESTS` on B:,
pick the type the terminal is set to (for VT52, leave it on VT100:
TERMTEST switches it with `CSI ? 2 l` and back with `ESC <`), then any
key turns the page and Esc goes back to the menu. One page each for cursor
addressing, cursor moves (`OK` on every row when they are right), erasing,
inserting and deleting lines, scrolling, wrapping at the last column, line
graphics, the terminal's replies and the codes the keys send (Ctrl-C ends
that one). Each page says what it should look like. Install `Z80TESTS`
again from the monitor to get it onto a card that already has the disk.

### ALTSCR

`Z80TESTS` also carries `ALTSCR` (`src/machine/cpm/altscr.asm`), a check of the
alternate screen with the VT100 terminal type: run `B:ALTSCR`. It fills the
main screen with numbered rows in bold, underlined yellow and reverse, with
an X in the middle, then goes to the alternate screen with `?1049`, `?1047`
and `?47` in turn, scrolls it, and comes back on a key: the main screen
must be as it was. `?1049` also brings back the cursor and its colors, so
`<1049` appears after the X in bold red. The last step asks you to open
and close SET-UP over the alternate screen, whose rows must come back.
Any key goes on; Ctrl-C leaves at once.

### Moving files

`CPM22` (and `CPM32` for CP/M 3) carries three tools (`src/machine/cpm/xls.asm`, `xpip.asm`, `xera.asm`)
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
  with its tools, `MEM`, `HWINFO`, `AYTEST`, `AYPLAY` and its tunes, `AYTRACK`: boots), `Z80TESTS` (`TERMTEST`, `ALTSCR`, and the Z80 instruction exercisers
  `EXZ80DOC` and `EXZ80ALL`, about 10 minutes each at full speed, and the
  `CPUTEST` diagnostics; `PRELIM` needs an emulator that runs block moves
  in one go, as z80pack's cpmsim does, and wrecks CP/M here: reset after
  it), `EMPTY` (a blank disk), CP/M 3's `CPM31` (boots), `CPM32` and
  `CPM3GEN` (see [Other operating systems](#other-operating-systems)), and
  `F83` ([Forth](#forth-f83)).
  They are kept compressed in flash (about 810 KB) to start a blank card
  with; existing files are only replaced if
  you say so.
- **F**: format the SD card, for a new card or one the monitor cannot
  read ("no FAT file system"). Everything on the card is lost, so it asks
  you to type `FORMAT` and Return first (Esc or anything else leaves it
  alone). The format follows the card's size: up to 2 GB FAT16 (FAT12 on
  the smallest), FAT32 above it (4 KB clusters below 4 GB, 32 KB up to
  64 GB, 64 KB beyond), in one partition with the clusters on 4 MB
  boundaries, as the SD Association lays cards out. Cards over 32 GB get
  FAT32 rather than exFAT, which the firmware does not read; PCs read
  FAT32 cards of any size. A large card takes some seconds. Then **I**
  puts the built-in disks on it.
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
  card, Return mounts and saves). CP/M 2.2 sees a changed drive after a
  warm boot, so type **^C** at the prompt afterwards (as after swapping
  floppies); CP/M 3 sees it at once. Don't change a drive a program is
  using.

  **Drive sets**: the Set row above A: steps through named sets of disks
  (Left/Right or Space), each filling in A-H; Return mounts them as
  above. It shows the set the drives match. **S** saves the drives as a
  set (type a name of up to 8 letters and digits; an existing name is
  replaced), **R** or **Delete** removes the set shown (after **Y**). To
  switch systems, pick a set, Return, then **F11** (reset). Sets are
  files in `/DISKS80/SETS`, `NAME.SET`, one line a drive as in
  `DRIVES.CFG`, so they can be written on a PC too:

  ```
  A=CPM31
  B=CPM32
  E=HD8
  ```

  Drives left out are empty; a disk not on the card is left out (the panel
  says how many). The monitor knows only what is mounted now.

  The monitor's install (**I**) writes two to start with, if they are
  missing: `CPM22` (CPM22, Z80TESTS) and `CPM3` (CPM31, CPM32); after
  that they are yours to change or remove.

  **RAM disk** (boards with PSRAM: `frankless`, `feather-rp2350-hstx-psram`
  and its Adalogger variant): the PSRAM left over after the Z80's banks
  is a drive image named `RAMDISK`, in the panel's list (and the
  monitor's **M** and **L**, also without a card) with the card's images.
  It is the largest format that fits in what is left: a 4 MB hard disk
  with an 8 MB PSRAM (an 8 MB one with 16 MB); the start-up lines say
  which. It is fast and wears nothing, but it starts blank at power-on and
  every reboot (Mode change, SET-UP's R, a firmware update) and is lost at
  power-off: copy what you want to keep to a card image (`PIP`). It keeps
  its files while the board runs, also when unmounted or after **F11**.
  `DRIVES.CFG` and the sets can name it like any image (a `RAMDISK.DSK`
  on the card is never used, and **N** does not make one).
- **M** returns to the monitor.

**F11** is the reset button: it reboots from drive A at once (keys typed
ahead are dropped; with drive A empty it goes to the monitor). It resets the
terminal too, as RIS does (colors, modes, scrolling region, an unfinished
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
| 8 | the disk in a drive, for our BIOSes (ours, not picosim's): OUT the drive, IN its format (0 none, 1 8" floppy, 2 4 MB, 3 8 MB), IN again the count of images mounted on it. A hard-disk image is refused to a system that has not asked about its drive since the reset |
| 10 | file transfer: files in the `/XFER` folder of the SD card or a USB stick, a 128-byte record at a time by DMA, for `XLS`, `XPIP` and `XERA` (ours; `src/machine/machine.h` has the commands) |
| 64 | MMU: bank 0 plus `MACHINE_BANKS` 48 KB banks (default 2: enough for CP/M 3; MP/M II uses as many as it was generated for, see [Other operating systems](#other-operating-systems)), or with `PSRAM_BANKS` up to 15 in the PSRAM; reads the number of banks in the high nibble (so 15 at most) and the selected one in the low |
| 65/66 | clock: the board's real-time clock, or without one 1 Jan 2026 at power-on; writing it is ours (CP/M 3's `DATE SET`), see [The clock](#the-clock) |
| 12 | hardware information, for `HWINFO` (ours; see [HWINFO](#hwinfo)) |
| 67 | 60 Hz timer interrupt |
| 68/69 | a millisecond count, low byte then high (ours: for timing without interrupts, as the CPU has no fixed speed) |
| 160 | hardware control |
| D8h/D0h | the AY-3-8910 sound chip: register select, data (RC2014's; with the firmware's stereo register 16, see [Sound](#sound)) |

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

A full power cycle after an update (unplug the board, plug it back in) is
recommended: the USB host (keyboard, sticks) has been seen not working
after the update's restart until the power was cycled. The panel says so
while it installs.

If the power fails during step 3 the board will not start: flash it over
USB with BOOTSEL as in `docs/FLASHING.txt`, which always works.

## Building

Everything installs under `.toolchain/` in the repo (git-ignored); only
`curl`, `git`, `python3`, `xz` and a host C/C++ compiler with `make` are
needed.

```sh
git submodule update --init          # pico-sdk and TinyUSB (not pico-sdk's submodules; z80pack is vendored)
scripts/setup-toolchain.sh           # CMake, Arm GNU Toolchain, picotool, clang-tidy
scripts/test.sh                      # host unit tests
scripts/build.sh                     # -> build/pico2-dvi-sock/dvi_terminal.uf2, build-info.txt
scripts/package.sh                   # every built target -> dist/dvi-terminal-<rev>.zip, dist/CHANGES.md
scripts/lint.sh                      # clang-tidy over the tests' and the firmware's builds
scripts/docs.sh                      # the user guide -> build/docs/html/index.html (also in the release ZIP as guide/)
```

`scripts/lint.sh` runs clang-tidy with the checks in `.clang-tidy` (for
now: braces around every `if`/`else`/`for`/`while`/`do` body) on our
sources, as `build/test` and `build/<target>` compile them, so run
`scripts/test.sh` and `scripts/build.sh` first. CI runs it after the tests
and after each target's build.

`scripts/build.sh` takes `TARGET` (a board header in `boards/`, default
`pico2-dvi-sock`; see [Other boards](#other-boards)), `CMAKE_ARGS` (extra configure
arguments, e.g. `CMAKE_ARGS=-DVIDEO_DIAG=ON`) and `SDCARD` (a mounted card
to copy `FIRMWARE.UF2` to). It also writes `build/<target>/FIRMWARE.UF2`
and runs `scripts/check_sram_only.py`, which fails the build if code that
must never read the flash could reach code or data left there: the firmware
update's final copy, the video core, the clock change and the flash
writes. Plain
`cmake -S . -B build/x` also works and picks up `.toolchain/`
automatically.

Toolchain versions are pinned in **`toolchain.env`**, the single source of
truth for `scripts/setup-toolchain.sh`. To upgrade, change the version and
its SHA256 there, and the matching `ARG`s in the CI build image's Dockerfile
(gitlab.seg9.com `images/build-image`, which installs the toolchain in
`/opt/pico-toolchain`; until it is rebuilt, CI downloads the new versions in
every job). pico-sdk is upgraded by moving its submodule (and the image's
`PICO_SDK_*`, which picotool is built against). TinyUSB is our own
submodule (`lib/tinyusb`, the top `CMakeLists.txt` sets `PICO_TINYUSB_PATH`),
pinned to an upstream master commit after 0.21.0 for its RP2350 host fixes
(control-transfer timeouts, aborting failed transfers, recovering a stuck
hub); move it on its own, and check the firmware's `__end__`.

### Build options

| CMake option | Default | |
|---|---|---|
| `STARTUP_LOGO` | ON | show the startup logo at power-on/reset |
| `LOGO_IMAGE` | `assets/logo/ColorLogo-576.png` | the logo's PNG (see [Startup logo](#startup-logo)) |
| `LOGO_MS` | 10000 | how long the logo shows, in ms |
| `LOGO_IN_FLASH` | ON | experimental: keep the logo data in flash, copied out only while it shows (saves about 19 KB of SRAM); OFF copies it into SRAM with the program |
| `STARTUP_DEMO` | OFF | run the demo after the logo |
| `MACHINE_BANKS` | 2 | Z80 machine: 48 KB banks besides bank 0 (each costs 48 KB of SRAM; a third does not fit) |
| `PSRAM_BANKS` | OFF | Z80 machine: the banks in the board's QSPI PSRAM (QMI chip select 1) instead, as many as fit up to 15, none in SRAM, and the rest a [RAM disk](#running-cpm); set by `boards/feather-rp2350-hstx-psram.h`. Without a PSRAM chip at boot the machine has no banks |
| `DVI_MODE` | the board's | the Display setting's default (until one is saved) and `hstx_test`'s mode: `1024x600` (60 Hz, reduced blanking, a 44 MHz pixel clock: clk_hstx 220 MHz) or `800x600` (60 Hz, 40 MHz: clk_hstx 200 MHz). Empty (the default): the board header's choice, `pico_board_cmake_set(BOARD_DVI_MODE, ...)` (none makes one), else `1024x600`. clk_sys is the board's in both: 384 MHz, 336 on the Feathers (`BOARD_SYS_KHZ`, a multiple of 48 MHz). `HSTX_MODE`, until v0.10.0, is refused |
| `VIDEO_DIAG` | OFF | bring-up diagnostics on UART0 TX: boot markers, core 1 scanline timing, fault reports |
| `COPY_TO_RAM` | ON | run from SRAM; must stay ON (see [How it works](#how-it-works)) |

## How it works

Video comes from the RP2350's HSTX, whose TMDS encoder turns RGB565 pixels
into DVI (`src/video/hstx_dvi.c`, after Raspberry Pi's
`pico-examples/hstx/dvi_out_hstx_encoder`): 1024x600 at 60 Hz with reduced
blanking, a 44 MHz pixel clock, so clk_hstx 220 MHz, or 800x600 at 60 Hz, a
40 MHz pixel clock, so clk_hstx 200 MHz, from the system PLL; clk_sys runs
at 384 MHz (1.3 V; the Feathers 336) from the USB PLL, which also gives USB
its 48 MHz. While the screen is blanked the DVI pins are left undriven, so the
monitor sleeps; 3 s after waking the signal stops for half a second once
more, for a monitor that woke without locking to it. Two DMA channels
feed it blanking command lists and scanlines in turn. `hstx_test.uf2`,
built alongside, shows a test pattern for checking a monitor and the
clocks.

- **Core 0** runs the terminal: UART0 receive interrupt into a ring buffer
  (`src/platform/serial.c`), the escape sequence parser (`src/term/vtparse.c`, Paul
  Williams' DEC state machine), the terminal (`src/term/term.c` with the
  personalities `src/term/vt100.c`, `vt52.c` and `adm3a.c`) on a cell
  buffer (`src/screen/screen.c`), rasterization of changed lines (glyph bits per
  cell with the attributes applied, and each cell's colors:
  `src/render/raster.c`, driven by `video_update` in `src/video/video.c`), and the USB
  keyboard: TinyUSB host (`src/usb/usbkbd.c`) with boot protocol reports turned
  into VT100 key codes (`src/input/keyboard.c`). In CP/M mode it also runs the
  Z80.
- **Core 1** composes each scanline from those into RGB565 pixels a couple
  of lines ahead of the DMA, inverts the cursor cell in the blink-on phase,
  and decodes the startup logo (`src/video/video.c`, `raster_compose`); the DMA
  interrupt runs on core 1 too.

The firmware runs entirely from SRAM (`COPY_TO_RAM`): the boot-time flash
(XIP) setup is chosen for clk_sys up to 150 MHz, far below the 384 MHz the
firmware runs at.

### Fonts and glyph codes

The screen buffer stores one-byte glyph codes (see `src/screen/font.h`): ASCII at
0x20-0x7E, Latin-1 at 0xA0-0xFF, DEC Special Graphics (line drawing) at
0x01-0x1F, and shades/blocks/double-line box drawing at 0x80-0x9F. The
tables are generated at build time: Terminus 10x20 for 80 columns (bold
text in Terminus's bold face) and misc-fixed 6x12 at double height for 132
(no bold face fits 6 pixels, so bold text there is only brighter, as on a
VT100). They stay in flash; the current width's fonts are copied into
SRAM when the width changes. Glyphs a font lacks (control pictures,
scan lines, half blocks) are drawn by `font_gen.py`.

### Startup logo

`LOGO_IMAGE` is converted at build time by `src/render/logo_gen.py` (Python
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
program runs from RAM. The flash is written with the 384 MHz clock
running: the ROM's erase/program routines run its serial clock at
clk_sys / 12, and reads are set back to at most 50 MHz afterwards (see
`src/platform/nvflash.c`).

### Resources used

As of v0.5.0 with the default build (`MACHINE_BANKS=2`):

| Resource | Used | Free |
|---|---|---|
| SRAM (520 KB) | about 475 KB: code 170 KB, data 20 KB, buffers 285 KB (Z80 memory 160 KB, raster memory 60 KB, the current fonts 20 KB, screen 13 KB, ...) | about 37 KB, plus most of the two 4 KB scratch banks. A third Z80 bank (48 KB) does not fit |
| Flash (4 MB) | firmware 496 KB; firmware-update staging area (upper 2 MB, used only during an update); settings (2 sectors); the last sector is left blank | about 1.5 MB below 2 MB for the firmware to grow into |
| Clocks | clk_sys 384 MHz at 1.3 V (from the USB PLL; the Feathers 336), HSTX 220 MHz (200 at 800x600), USB 48 MHz | |
| Peripherals | HSTX (DVI), 2 DMA channels, UART0, SPI0 (SD card), USB (host), TIMER0, watchdog; with a DAC (Frankless) also a PIO state machine, 2 DMA channels and DMA_IRQ_1 for the sound | 14 DMA channels (12 with a DAC), all three PIO blocks (but a state machine), UART1, SPI1, I2C0/1, PWM, ADC, TIMER1, TRNG, SHA-256, the QMI's second chip select (PSRAM) |
| GPIO | GP0/1 UART0, GP2/3 RTS/CTS (Flow control RTS/CTS only), GP4-7 SD card, GP12-19 DVI Sock | GP8-11, GP20-22, GP26-28 (GP26-28 can be analog inputs); GP2/3 are the board header's CTS/RTS, so keep them free |

## Project layout

| Path | Contents |
|---|---|
| `src/` | The firmware's libraries; see below |
| `apps/` | The programs: `terminal/` (the firmware) and `hstx_test/` (HSTX test pattern) |
| `ld/` | Linker script override: the cold code that stays in flash |
| `boards/` | Board headers: pins, flash, crystal per build target; `example.h` is the template (see [Other boards](#other-boards)) |
| `test/` | Host unit tests (plain CMake + gcc with ASan/UBSan, no pico-sdk) |
| `tools/hostbridge.py` | Run a host program with the terminal as its display (see [Trying it from a PC](#trying-it-from-a-pc)) |
| `lib/pico-sdk` | Pinned submodule (not its own submodules) |
| `lib/tinyusb` | Pinned submodule: TinyUSB, newer than pico-sdk's 0.18 (its USB host fixes); for now our fork's `dvt/integration` branch (github.com/arlaneenalra/tinyusb): upstream master plus the RP2350 host fixes proposed upstream (`docs/upstream/`) |
| `lib/z80pack` | Vendored part of z80pack (Z80 core, z80asm, picosim disk images); `UPSTREAM.md` gives the commit and local changes |
| `lib/fatfs` | ChaN's FatFs R0.15a, vendored (see its README) |
| `assets/logo/` | The startup logo (PNG used by the build, JPEG original) |
| `assets/fonts/` | Terminus Font 10x20 and its bold face (SIL OFL 1.1) and misc-fixed 6x12 (public domain); see its README |
| `cmake/build_info.cmake` | Generates `term_build.h` (git describe, commit, date) on every build |
| `cmake/dvt.cmake` | Helpers for the libraries, shared by the firmware and host tests |
| `scripts/` | `setup-toolchain.sh`, `build.sh`, `test.sh`, `lint.sh`, `package.sh`, `changelog.sh` |
| `LICENSE` | The project's MIT license |
| `toolchain.env` | Pinned CMake, Arm GNU Toolchain and picotool versions |
| `docs/FLASHING.txt` | End-user flashing steps, shipped in the package |
| `docs/cpmtools-diskdefs` | cpmtools definition of the 8 MB disk format |
| `docs/THIRD-PARTY.md`, `docs/licenses/` | Third-party components and licenses (shipped, with the texts from the submodules, in `licenses/`) |

The code is in libraries, one directory each under `src/` (static
libraries `dvt_<name>`, after
[CGA_LCD_Capture](https://github.com/arlaneenalra/CGA_LCD_Capture)): a
library's public headers sit next to its sources, and a header only its
own files share is named `<name>_internal.h`. A library is either portable
(no pico-sdk; the host tests build it) or RP2350-only. `cmake/dvt.cmake`
has the helpers; `src/CMakeLists.txt` lists the libraries.

| Library | | |
|---|---|---|
| `common` | portable | `crc32`, `lz` (LZ4-format decoder), `scratch` (the shared 4 KB buffer), `xonxoff` (XON/XOFF flow control's decisions) |
| `clock` | portable | `datetime` (dates without the C library's `localtime`, FAT timestamps), `wallclock` (the time kept between reads of the RTC) |
| `rtc` | portable / RP2350 | `rtc_chip.h` (the chip-driver interface, on a register bus), `ds3231` and `pcf8523` (the drivers): portable `dvt_rtc_chips`; `rtc` (the wall clock and the board's RTC on I2C): RP2350 `dvt_rtc` |
| `screen` | portable | `screen` (cell buffer, saved rows), `palette` (the 256-color palette per Colors/Palette setting), `font.h` (glyph codes) |
| `term` | portable | `term.[ch]` (the terminal) with its personalities `vt100.c`, `vt52.c` and `adm3a.c` (sharing `term_internal.h`), `vtparse` (escape sequence parser) |
| `render` | portable | `raster` (cells to pixels), `splash` (startup logo); generated font tables and logo (`font_gen.py`, `logo_gen.py`) |
| `input` | portable | `keyboard` (HID keys to VT100 codes), `keyname` (key names for the status line), `blank` (screen blanking timer) |
| `settings` | portable | `settings` (settings records), `uf2` (UF2 file checks for the firmware update) |
| `machine` | portable | `machine` (the Z80 machine: z80pack glue; `z80pack/` holds its configuration headers), `cpm/` (the CP/M 2.2 BIOS, boot loader, WORMS, XLS, XPIP, XERA, TERMTEST, ALTSCR, MEM, HWINFO, AYTEST, AYPLAY and AYTRACK, `tunes/` (AYPLAY's PSG tunes, made at build time), `f83/` (F83's VIEWDRV.BLK and AY.BLK), and in `cpm3/` CP/M 3's banked BIOS, made into `CPM3.SYS` at build time; see its README) |
| `sound` | portable | `ay8910` (the AY-3-8910), `mixer` (the bell and the AY into the DAC's stream), `tune` (SET-UP's demo tune and chime) |
| `disks` | portable | `drives` (disk images on FatFs), `drivecfg.c` (`DRIVES.CFG`, the image list and the drive sets), `diskimg` (the built-in images; `diskimg_gen.py` compresses them), `xfer` (port 10's files: `/XFER`), `usbvol` (which USB stick is `USB:`) |
| `ui` | portable | `setup` (SET-UP panel), `drivepanel` (SET-UP's Drives panel), `fwpanel` (SET-UP's firmware update), `monitor` (the CP/M machine's monitor), `demo` |
| `board` | RP2350 | `board.h` (the board header's pins, checked) |
| `platform` | RP2350 | `serial` (UART with RTS/CTS or XON/XOFF and a TX queue), `led`, `audio` (the sound: I2S to the DAC by PIO and DMA, the bell), `nvflash` (settings sectors, written at the DVI clock), `fwupdate` (firmware update), `extram` (the PSRAM), `diag` (`VIDEO_DIAG`) |
| `usb` | RP2350 | `usbkbd` (USB keyboards, the USB log, restarting a stuck host), `tusb_config.h` (TinyUSB host) |
| `storage` | RP2350 | `diskio.c` (FatFs drives: 0 the SD card, 1 a USB stick), `sdcard` (SPI SD card), `usbmsc` (USB sticks) |
| `video` | RP2350 | `hstx_dvi` (HSTX DVI output), `video` (scanlines on core 1) |

The programs are in `apps/`: `apps/terminal/` (`dvi_terminal.uf2`:
`main.c` with `setup_glue.c`, `cpm_glue.c`, `status.c` and
`input_glue.c`, sharing `terminal.h`) and `apps/hstx_test/` (the HSTX
test pattern). FatFs (`lib/fatfs`, `lib/fatfs.cmake`) and z80pack's CPU
(`lib/z80pack`, built by `src/machine`) are vendored.

## CI and releasing

GitLab CI (`.gitlab-ci.yml`) and GitHub Actions (`.github/workflows/ci.yml`,
on the GitHub mirror) run the same jobs from the same scripts; on GitHub a
`v*` tag makes a GitHub Release with the ZIP. GitHub's runners install the
toolchain with `scripts/setup-toolchain.sh` (cached) instead of the build
image.

- `test`: host unit tests (JUnit report).
- `build`: firmware per target (a matrix: `pico2-dvi-sock`, `frankless` and
  the four Feather targets);
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
(`src/video/hstx_dvi.c` from pico-examples; `src/machine/machine.c`, `src/machine/z80pack/` and
`src/machine/cpm/` from z80pack's picosim, also MIT). The third-party parts:

Terminus Font is © Dimitar Toshkov Zhekov under the SIL Open Font License
1.1; see `assets/fonts/Terminus-OFL.txt`. The tables generated from it
are derivatives and stays under that license. The misc-fixed 6x12 font is
public domain. pico-sdk is BSD-3-Clause, as is the pico-examples HSTX DVI
example `src/video/hstx_dvi.c` follows.

TinyUSB is MIT; pico-sdk's CMSIS headers are Apache-2.0.
z80pack is © Udo Munk and others under the MIT license (`lib/z80pack/LICENSE`);
`src/machine/machine.c` and `src/machine/z80pack/` follow its picosim, and its CPU core
includes `log.h` (Apache-2.0). FatFs is © ChaN under a 1-clause BSD license
(`lib/fatfs/LICENSE.txt`). The built-in disk images come from z80pack: CP/M
2.2 and CP/M 3 are Digital Research's, redistributed under the right to use,
distribute and modify CP/M and its derivatives that DRDOS, Inc. (Digital
Research's successor) stated in 2022, widening Lineo's 2001 grant to "The
Unofficial CP/M Web Site" (http://www.cpm.z80.de/license.html); the Z80 exercisers are Frank D. Cringle's, GPL-2.0 or later (their sources are
on the same disk image).

[`docs/THIRD-PARTY.md`](docs/THIRD-PARTY.md) lists every third-party
component in the firmware with its license; release packages carry it and
the license texts in `licenses/`.
