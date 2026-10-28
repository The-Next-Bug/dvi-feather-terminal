# The Z80 CP/M machine

With SET-UP's **Mode** set to *CP/M machine*, the board is a small CP/M
computer: a Z80 emulated by the RP2350 (z80pack's CPU core, as in its
picosim for the Pico) with the screen as its console, the USB keyboard as
its keyboard, disk images on a microSD card, and the serial port as its
second serial port. Set Mode back to *terminal* for the plain terminal.
Changing Mode saves and restarts the board.

## Getting started

1. Wire an SD card (see [Boards](../start/boards.md)) and put in a card
   with a FAT file system (cards up to 32 GB usually come that way;
   otherwise the monitor's **F** formats it).
2. Set Mode to *CP/M machine* in SET-UP and save. The board restarts into
   the [monitor](monitor.md).
3. Press **I** to install the built-in disks onto the card, then Return to
   boot CP/M 2.2 from drive A.

```
A>DIR
A: ASM      COM : BYE      COM : DDT      COM : DUMP     COM
...
A>HWINFO
```

## The machine

- **CPU**: a Z80, unthrottled: it runs in 10 ms slices between the
  keyboard, disk and screen work, at roughly 40-50 MHz of Z80 time on a
  Pico 2 (the monitor's **S** measures it; the status line shows it while
  CP/M runs).
- **Memory**: bank 0 (64 KB) plus 48 KB banks for banked systems: two in
  SRAM on most boards, up to 15 in the PSRAM on boards with one (the
  Frankless, the PSRAM Feathers), where the rest of the PSRAM is a
  [RAM disk](drives.md#the-ram-disk).
- **Disks**: eight drives, A-H, each a disk image on the SD card (8" floppy,
  4 MB or 8 MB hard disk). See [Drives and disk images](drives.md).
- **Devices**: picosim's console, second serial port, disk controller,
  MMU, clock and timer, plus this firmware's own: the drive format port,
  file transfer to the SD card and USB sticks, hardware information, a
  millisecond tick, and an AY-3-8910 sound chip. See
  [Z80 I/O ports](../reference/ports.md).

## Keys

| Key | Action |
|---|---|
| **F11** | the reset button: reboots from drive A at once (keys typed ahead are dropped; with drive A empty it goes to the monitor). It also resets the terminal, as RIS does, clears the screen and returns to the SET-UP width |
| **F12** | SET-UP; the CPU waits while it is open. **D** opens the [Drives panel](drives.md#the-drives-panel), **M** returns to the monitor |
| **Return** | sends CR alone, as CP/M expects, whatever the New line setting says |

`BYE.COM` and `RESET.COM` on `CPM22` use the hardware control port:
`BYE` stops the machine (back to the monitor), `RESET` reboots it.

## Sources

Digital Research's own sources for CP/M 2.2 and CP/M 3, its tools and much
else are on [The Unofficial CP/M Web Site](http://www.cpm.z80.de/) (its
[source page](http://www.cpm.z80.de/source.html)). The disks here are
built from z80pack's binaries, with BIOSes of this project's own.
