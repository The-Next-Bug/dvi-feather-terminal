# How it works

## Video

The RP2350's HSTX peripheral has a TMDS encoder, so DVI needs no CPU work
per pixel: the CPU supplies RGB565 scanlines and the HSTX serializes them
(`src/video/hstx_dvi.c`, after Raspberry Pi's pico-examples).

- **Clocks**: the HSTX (clk_hstx) runs at 5 x the pixel clock: 220 MHz
  for 1024x600, 200 MHz for 800x600. It is the system PLL's only user,
  which runs at twice that (440 or 400 MHz) and feeds nothing else. The
  CPU (clk_sys) runs from the USB PLL at 384 MHz (336 on the Feathers),
  which also gives USB, the ADC and the peripherals their clocks. So the
  CPU's speed does not depend on the display mode, and no part of the
  chip is clocked above 384 MHz.
- **Two DMA channels** take turns feeding the HSTX: blanking command lists,
  then each active line's commands and pixels, reloaded by an interrupt.
- **The screen** is a buffer of cells: a glyph code, colors and attributes
  each (`src/screen/screen.c`). When a line changes, core 0 renders it
  into bit strings, one per scanline of the row, with the attributes
  applied (`src/render/raster.c`).
- **Core 1** composes each scanline from those bits and the palette into
  RGB565 pixels a couple of lines ahead of the DMA, and inverts the cursor
  cell (`src/video/video.c`, `raster_compose`). At 132 columns it takes
  about half of the time a scanline lasts.
- **Blanking**: the HSTX stops and its pins are left undriven, so the
  monitor sleeps; waking restarts it at a frame start, and a second start
  follows 3 s later.

## Core 0

The UART's receive interrupt fills a ring buffer (`src/platform/serial.c`);
the main loop feeds it to the escape sequence parser (`src/term/vtparse.c`)
and the terminal (`src/term/term.c` with its personalities), renders
changed lines, polls the USB host (TinyUSB; `src/usb/usbkbd.c` turns
keyboard reports into key codes with `src/input/keyboard.c`), and in CP/M
mode runs the Z80 in 10 ms slices (`src/machine/machine.c` on z80pack's
core).

## Memory

The firmware runs entirely from SRAM: the boot-time flash setup is for
clocks up to 150 MHz, far below 384. Some code that is used rarely stays
in flash and runs from there through the flash cache, at 50 MHz (the
monitor, SET-UP's panels, FatFs, parts of TinyUSB). Nothing may run from
flash on core 1, during the clock change, while the flash is written, or in
the firmware update's final copy; `scripts/check_sram_only.py` checks that
at every build.

SRAM is the tight resource: the Z80's memory (160 KB on boards without
PSRAM), the raster (85 KB), the fonts in use (24 KB), the screen and the
saved rows. The PSRAM boards keep the Z80's banks and a RAM disk in PSRAM.

## Flash

| Area | Use |
|---|---|
| from the start | the firmware (about 1.2 MB: code, fonts, the logo, the built-in disk images, compressed) |
| upper half | the firmware update's staging area, used only during an update |
| two sectors before the last | the settings, used in turn |
| the last sector | left blank (UF2 flashing erases it) |

## Settings

A record of one byte a setting, with a sequence number and a CRC-32, in
two flash sectors used in turn; settings are only ever appended, so an
older record lacks the newer ones, which keep their defaults
(`src/settings/settings.c`).

## The USB host

TinyUSB (a fork with fixes for the RP2350's host controller, proposed
upstream) with the boot keyboard and mass storage classes. One hub. A
watchdog restarts the USB host when a device stays half set up or the hub
stops answering; see [Troubleshooting](../terminal/troubleshooting.md).
