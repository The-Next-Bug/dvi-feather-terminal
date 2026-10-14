# Idea: this firmware on the Friendly fZ80 (Teensy 4.1)

Status: speculation, not planned (bd: see the deferred ticket under the
Board designs epic). Written 2026-10-13 from a read of the sibling
`fz80-firmware` repository (fZ80.7-27Jul2026, board v6b).

The Friendly fZ80 is Tom Jennings' Z80 CP/M computer built around a Teensy
4.1. Its firmware is his own (personal-use licence). This note sketches
what running *this* firmware on that hardware would take. Nothing here
copies or reuses his code.

## Hardware compared

| | This firmware (RP2350) | Friendly fZ80 (Teensy 4.1, i.MX RT1062) |
|---|---|---|
| CPU / RAM | 2 x Cortex-M33 at 400 MHz, 520 KB SRAM | 1 x Cortex-M7 at 600 MHz, 1 MB (512 KB TCM + 512 KB OCRAM), optional 8-16 MB PSRAM |
| Video | HSTX -> DVI 800x600, RGB565, scanlines composed on core 1 | FlexIO2 + DMA -> VGA, 4-bit RGBI (16 colours), up to 1024x768, framebuffer |
| Keyboard, sticks | TinyUSB host on the native USB | USB host port (fZ80 uses USBHost_t36) |
| Storage | one SD card, SPI | internal microSD on 4-bit SDIO, plus two removable SD slots on SPI1 with card detect, write protect and red/green LEDs |
| Serial | UART0/1, optional RTS/CTS | Serial2 on a DE-9: CTS and CD in; RTS/DTR only through an I2C expander |
| Settings | two flash sectors | EEPROM emulation (fZ80 keeps keymaps and its boot monitor there) |
| Clock | none (bd 1ls.6) | Teensy RTC with coin cell |
| Extras | LED | rotary encoder + switch, piezo buzzer, power LED red/green and a power-on pin, parallel port (three TCA9554: two 8-bit ports with handshake) |
| Z80 | z80pack core, picosim port map, our BIOSes | davidly's emulator, his own port map (0x10 keyboard, 0x30 serial, 0x40 disk, 0x50 RTC, 0x60 VGA, ...), his BIOS and EEPROM monitor, CP/M and MP/M |

## What carries over unchanged

Every portable, host-tested module: screen, vtparse, term with the VT100 /
VT52 / ADM-3A personalities, keyboard, keyname, blank, settings, setup,
drivepanel, fwpanel's UI, machine.c with the z80pack core, drives, xfer,
monitor, usbvol, lz, diskimg, crc32, and all of src/machine/cpm (BIOSes, tools,
built-in disks). That is most of the logic: the "no pico-sdk headers in
portable modules" rule is what makes a port thinkable.

## What gets a new implementation

1. **Video.** One core, so no just-in-time scanline composition.
   `video_update` draws dirty cells into a 4-bit framebuffer the FlexIO/DMA
   scans out (the VGA_4BIT_T4 model; that library is wwatson's, separate from
   the fZ80 code). 800x600x4 bpp = 240 KB in OCRAM. raster.c's glyph and
   attribute logic stays; its output becomes 4-bit pixels. The palette drops
   to 16 colours: 256-colour and 24-bit SGR go through `palette_nearest`;
   mono/phosphor become 16-level ramps at best.
2. **USB host.** USBHost_t36 behind usbkbd/usbmsc, or TinyUSB, whose ChipIdea
   host driver covers the RT1062's second controller. TinyUSB keeps
   usbkbd.c/usbmsc.c and their enumeration lessons nearly as they are: try
   it first.
3. **Storage.** diskio.c grows to four FatFs volumes: internal microSD
   (SDIO, needs a uSDHC driver), the two removable slots (sdcard.c over a
   Teensy SPI backend), the USB stick. Card-detect makes bd 1ls.11 real;
   the slot LEDs become drive lights.
4. **Serial.** serial.c on the LPUART. CTS in hardware, RTS only via I2C:
   RTS/CTS is half there, so XON/XOFF (bd dmt.27) matters more.
5. **Settings.** nvflash on EEPROM emulation or a file on the internal card.
6. **Firmware update.** UF2 checking and staging do not apply; a
   FlasherX-style update (HEX file from SD, staged in spare flash, copied
   over) would replace it. Last.
7. **Build.** pico-sdk CMake -> CMake against the Teensy `cores/teensy4`
   sources and linker script (keeps the host tests and the CP/M build
   steps), or arduino-cli as fz80-firmware does. COPY_TO_RAM,
   check_sram_only.py and the flash-clock rules go away.

## What the fZ80's hardware would add

Most of it lands on tickets that already exist:

- RTC (bd 1ls.6): Teensy RTC with coin cell, so CP/M clock ports, file
  dates and DATE work with no I2C chip.
- Bell (bd 1ls.7): the piezo.
- Button (bd 1ls.12): the encoder and its switch. Held at reset for the
  monitor (as on the fZ80); turn and press to drive SET-UP without a
  keyboard.
- LEDs (bd 1ls.8, 1ls.15): power LED red/green for heartbeat and status;
  slot LEDs for disk activity.
- Printer: the parallel port gives machine.c's `printer_out` hook a home
  (picosim's printer ports 5/6), and the CP/M 2.2 BIOS LIST stub becomes
  real.
- Memory (bd 1ls.5): EXTMEM PSRAM if fitted; even without it, 1 MB holds
  more Z80 banks than the RP2350.

## Open questions

- **Which Z80 machine?** The fZ80's CP/M and MP/M disks need its port map,
  BIOS and EEPROM monitor. (a) Treat the fZ80 as another board for this
  system: picosim map, our disks. Cheapest. (b) An fZ80-compatible port map
  as a second machine personality, written fresh from its port
  documentation, after settling the licence question (the fZ80 code is
  personal-use only and cannot be copied). Do (a); keep (b) as research.
- **Platform split.** Partly done by the library restructure (bd gr1):
  the RP2350 code is in its own libraries (src/board, platform, usb,
  storage, video), apart from the portable ones, and the program is
  apps/terminal. Its files still call pico-sdk directly (time, GPIO,
  watchdog, multicore). A port needs a thin `platform.h` (time, reboot,
  LED, idle) for apps/terminal, and Teensy 4.1 counterparts of the RP2350
  libraries. Worth doing on its own before any second architecture.
- **Windows and H19.** The fZ80 has four VGA windows with H19/H89 escapes.
  An H19 personality fits the term personality split (vt52.c is close: the
  H19 is a VT52 plus extensions). Windows would be new screen-level work.

## Rough order

1. Platform split on the RP2350, no behaviour change.
2. Teensy build skeleton with the host tests; terminal only (VGA, serial,
   USB keyboard).
3. FatFs on the internal SDIO card, then the CP/M machine.
4. Removable slots, USB sticks, RTC, encoder SET-UP, buzzer, LEDs.
5. Printer port, firmware update; optionally an H19 personality and the
   fZ80-compatible machine research.

Step 1 is the largest single step; steps 2-3 are the port proper; the rest
reuses tickets already filed.
