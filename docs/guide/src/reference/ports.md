# Z80 I/O ports

The machine is z80pack's picosim, with devices of this firmware's own
added (`src/machine/machine.c`, `machine.h`). Unlisted ports read FFh and
ignore writes.

| Port | Device | Origin |
|---|---|---|
| 0, 1 | console: status, data | picosim |
| 2, 3 | second serial port: status, data (the board's UART at the SET-UP line settings) | picosim |
| 4 | disk controller | picosim |
| 5, 6 | printer (none) | picosim |
| 8 | the disk in a drive | this firmware |
| 10 | file transfer device | this firmware |
| 12 | hardware information | this firmware |
| 64 | MMU | picosim |
| 65, 66 | clock | picosim; setting it is this firmware's |
| 67 | 60 Hz timer interrupt | picosim |
| 68, 69 | millisecond tick | this firmware |
| 160 | hardware control | picosim |
| D8h, D0h | AY-3-8910 sound chip | RC2014's |

## Console (0, 1)

`IN 0`: bit 0 is **0** when a key is waiting (1 when none), bit 7 is 0
when output can be sent (always, here). `IN 1` reads the key, `OUT 1`
writes a character to the terminal. The
console is the [terminal](vt100.md), so programs send escape sequences
through it and read the terminal's replies.

## Second serial port (2, 3)

As the console, on the board's UART: the host connected to the serial port
(see [Connecting a host](../start/host.md)), at SET-UP's line settings and
flow control.

## Disk controller (4)

picosim's DMA disk controller: one port, a command byte with the drive in
the low nibble.

| Command | Action |
|---|---|
| `10h` | the next two `OUT`s set the address of a 4-byte disk command: track, sector (1-based), DMA address low, high |
| `20h + drive` | read that sector to the DMA address |
| `40h + drive` | write it from the DMA address |

`IN 4` reads the last command's status: 0 OK, 1 no such drive, 2 no disk,
3 seek error, 4 read error, 5 write error, 6 DMA error, 7 DMA address out
of range, 8 track out of range, 9 sector out of range. Sectors are 128
bytes; the geometry depends on the image's format (see
[Drives and disk images](../cpm/drives.md)).

A 4 MB or 8 MB image is refused (no disk) to a system that has not asked
about its drive on port 8 since the reset, so a BIOS that only knows 8"
floppies cannot read a hard disk as one and write over it.

## Drive format (8)

`OUT 8` a drive (0-7 for A-H); `IN 8` then reads its format: 0 none, 1 8"
floppy, 2 4 MB hard disk, 3 8 MB hard disk. Another `IN 8` reads a count of
the images mounted on it since power-on, so a BIOS can tell a changed disk.
See [Writing a BIOS](bios.md).

## File transfer (10)

Files in `/XFER` on the SD card or a USB stick, for `XLS`, `XPIP` and
`XERA`. See [File transfer device](xfer.md).

## Hardware information (12)

What the machine runs on, for `HWINFO`. See [Hardware information](hwinfo.md).

## MMU (64)

`OUT 64` selects a bank: 0 is the 64 KB bank; 1-n replace 0000H-BFFFH with
a 48 KB bank (C000H-FFFFH is common to all). `IN 64` reads the number of
banks in the high nibble (so at most 15) and the selected one in the low.
Selecting a bank that is not fitted stops the machine (`MMU: no bank n`).
Most boards have 2 banks in SRAM; the PSRAM boards have 15 in the PSRAM.

## Clock (65, 66)

`OUT 65` selects a register, `IN 66` reads it:

| Register | Value |
|---|---|
| 0, 1, 2 | seconds, minutes, hours |
| 3, 4 | the day number, low and high byte: days since 31 Dec 1977 (CP/M 3's) |
| 5 | the day of the month |
| 6 | the month, 0-11 |
| 7 | the year - 1900 |

In BCD, or in binary after `OUT 65,255` (which toggles it; `IN 65` reads 1
for binary). The day number is always binary.

`OUT 66` (this firmware's) sets the selected register: 0-2 (BCD or binary,
as read) replace that field of the current time; a write to 3 is kept for
the next write to 4, which sets the day number from both (the time of day
kept). Values out of range, and registers 5-7, are ignored. CP/M 3's BIOS
`TIME` (and so `DATE SET`) writes the day number, then hours, minutes,
seconds. The time is the board's real-time clock where it has one; see
[Clock](../terminal/clock.md).

## Timer (67)

picosim's 60 Hz timer interrupt: `OUT 67,1` starts it, `OUT 67,0` stops
it, and `IN 67` reads 1 while it runs.

## Millisecond tick (68, 69)

A free-running millisecond count, for timing without interrupts (the CPU
has no fixed speed): `IN 68` reads its low byte and latches the high byte
for `IN 69`. It wraps every 65.536 s.

```
        IN   A,(68)     ; low byte, latches the high
        LD   L,A
        IN   A,(69)     ; high byte
        LD   H,A        ; HL = milliseconds
```

## Hardware control (160)

Locked until `AAh` is written; then one command: bit 7 stops the machine
(`BYE.COM`), bit 6 resets it (`RESET.COM`). `IN 160` reads FFh while locked,
0 when unlocked.

## The AY-3-8910 (D8h/D0h)

As on Ed Brindley's RC2014 YM2149 board: `OUT D8h` selects a register,
`OUT D0h` writes it, `IN D0h` reads it. Registers 0-15 are the chip's;
clocked at 1.8432 MHz, so a tone of f Hz has period 115200 / f.

Register 16 is this firmware's (a real AY ignores it): the stereo layout.
Write 0 for SET-UP's Stereo setting, 1 mono, 2 ABC, 3 ACB. Reading it gives
what was written in bits 0-1 and the layout in use (1-3) in bits 4-5, so FFh
(older firmware, or a real chip) means no stereo. A machine reset writes 0
to registers 0-16: silence, and the layout back to SET-UP's.

Without a DAC (or with Sound DAC set to not fitted) the ports read FFh, as
with no chip. To find the chip, write FFh to register 1 and read it back:
the chip keeps four bits there, so it reads 0Fh (AYTEST also checks
register 2 with 5Ah).
