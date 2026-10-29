# DVI Feather Terminal

Firmware version: {{#include generated/version.md}}

The latest release's guide is online at
<https://the-next-bug.github.io/dvi-feather-terminal/>. Each release ZIP
carries its own in `guide/`.

The source code, the releases and the issue tracker are on GitHub:
<https://github.com/The-Next-Bug/dvi-feather-terminal>.

The DVI Feather Terminal turns a Raspberry Pi RP2350 board with a DVI or
HDMI connector into one of two machines, chosen in [SET-UP](terminal/setup.md):

- **A serial terminal**: a VT100 (or a VT52, or an ADM-3A) on a 3.3 V UART,
  with a monitor for its screen and a USB keyboard that types to the host.
- **A Z80 CP/M computer**: an emulated Z80 running CP/M 2.2 or banked CP/M 3
  from disk images on a microSD card, with the same screen and keyboard as
  its console.

![The terminal on a 1024x600 display](images/generated/terminal.png)

## What it does

- **Display**: 1024x600 or 800x600 at 60 Hz over DVI/HDMI; 80 or 132
  columns by 24 rows, plus a status line. Every pixel is the display's own:
  no scaling. See [Displays](start/displays.md).
- **Emulation**: VT100/VT102 with colors (16, 256 and 24-bit), the
  alternate screen of xterm, line drawing; the VT100's VT52 mode; a Lear
  Siegler ADM-3A. See [Terminal types](terminal/terminal-types.md) and the
  [escape sequence reference](reference/vt100.md).
- **Keyboard**: a USB keyboard (through one hub, with USB sticks beside it).
  See [Keyboard](terminal/keyboard.md).
- **Settings in flash**: line settings, colors, display mode, terminal
  type and more, edited in [SET-UP](terminal/setup.md).
- **Clock**: a real-time clock chip where the board has one, shown on the
  status line and used by CP/M. See [Clock](terminal/clock.md).
- **Sound**: on boards with an I2S DAC, the bell and an emulated
  AY-3-8910 sound chip for the Z80. See [Bell and sound](terminal/sound.md).
- **The Z80 machine**: eight drives, built-in CP/M 2.2 and CP/M 3 disks,
  tools to move files to and from the SD card and USB sticks, F83 Forth, a
  tracker and tune player. See [The Z80 CP/M machine](cpm/overview.md).
- **Firmware updates** from the SD card or a USB stick. See
  [Firmware updates](terminal/firmware-update.md).

## How this guide is organized

- **Getting started**: the boards, flashing, wiring a host, displays.
- **Using the terminal**: every feature of the terminal, one page each.
- **The Z80 CP/M machine**: the monitor, disks, CP/M, and a help page for
  each tool on the built-in disks.
- **Programming interfaces**: escape sequences, the keys sent, the Z80's
  I/O ports and the protocols of the devices this firmware adds.
- **Building and hacking**: building the firmware, adding a board, how it
  works inside, and where everything is in the source.

The settings tables, the board pin tables, the hardware information items
and the screenshots are generated from the firmware's own sources when the
guide is built, so they match the firmware it came with. The screenshots
are rendered by the firmware's display code, pixel for pixel.
