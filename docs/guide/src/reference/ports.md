# Z80 I/O ports

The machine is z80pack's picosim, with devices of this firmware's own
added (`src/machine/machine.c`, `machine.h`). Unlisted ports read FFh and
ignore writes.

Port numbers and the values written to and read from them are hex
throughout this page, marked with `h` (in the assembler examples, `H`, as
z80asm writes it): port `0Ah` is port 10 in decimal. Sizes and counts
(48 KB, 15 banks) are decimal.

| Port | Device | Origin |
|---|---|---|
| 00h, 01h | console: status, data | picosim |
| 02h, 03h | second serial port: status, data (the board's UART at the SET-UP line settings) | picosim |
| 04h | disk controller | picosim |
| 05h, 06h | printer (none) | picosim |
| 08h | the disk in a drive | this firmware |
| 0Ah | file transfer device | this firmware |
| 0Ch | hardware information | this firmware |
| 40h | MMU | picosim |
| 41h, 42h | clock | picosim; setting it is this firmware's |
| 43h | 60 Hz timer interrupt | picosim |
| 44h, 45h | millisecond tick | this firmware |
| A0h | hardware control | picosim |
| D8h, D0h | AY-3-8910 sound chip | RC2014's |

## Console (00h, 01h)

`IN 00h`: bit 0 is **0** when a key is waiting (1 when none), bit 7 is 0
when output can be sent (always, here). `IN 01h` reads the key, `OUT 01h`
writes a character to the terminal. The console is the
[terminal](vt100.md), so programs send escape sequences through it and
read the terminal's replies.

## Second serial port (02h, 03h)

As the console, on the board's UART: the host connected to the serial port
(see [Connecting a host](../start/host.md)), at SET-UP's line settings and
flow control.

## Disk controller (04h)

picosim's DMA disk controller: one port, a command byte with the drive in
the low nibble.

| Command | Action |
|---|---|
| `10h` | the next two `OUT`s set the address of a 4-byte disk command: track, sector (1-based), DMA address low, high |
| `20h` + drive | read that sector to the DMA address |
| `40h` + drive | write it from the DMA address |

`IN 04h` reads the last command's status:

| Status | Meaning |
|---|---|
| 00h | OK |
| 01h | no such drive |
| 02h | no disk |
| 03h | seek error |
| 04h | read error |
| 05h | write error |
| 06h | DMA error |
| 07h | DMA address out of range |
| 08h | track out of range |
| 09h | sector out of range |

Sectors are 128 bytes; the geometry depends on the image's format (see
[Drives and disk images](../cpm/drives.md)).

A 4 MB or 8 MB image is refused (no disk) to a system that has not asked
about its drive on port 08h since the reset, so a BIOS that only knows 8"
floppies cannot read a hard disk as one and write over it.

## Drive format (08h)

`OUT 08h` a drive (00h-07h for A-H); `IN 08h` then reads its format: 00h
none, 01h 8" floppy, 02h 4 MB hard disk, 03h 8 MB hard disk. Another
`IN 08h` reads a count of the images mounted on it since power-on, so a
BIOS can tell a changed disk. See [Writing a BIOS](bios.md).

## File transfer (0Ah)

Files in `/XFER` on the SD card or a USB stick, for `XLS`, `XPIP` and
`XERA`. See [File transfer device](xfer.md).

## Hardware information (0Ch)

What the machine runs on, for `HWINFO`. See [Hardware information](hwinfo.md).

## MMU (40h)

`OUT 40h` selects a bank: 00h is the 64 KB bank; 01h and up replace
0000h-BFFFh with a 48 KB bank (C000h-FFFFh is common to all). `IN 40h`
reads the number of banks in the high nibble (so at most 15, 0Fh) and the
selected one in the low. Selecting a bank that is not fitted stops the
machine (`MMU: no bank n`). Most boards have 2 banks in SRAM; the PSRAM
boards have 15 in the PSRAM.

## Clock (41h, 42h)

`OUT 41h` selects a register, `IN 42h` reads it:

| Register | Value |
|---|---|
| 00h, 01h, 02h | seconds, minutes, hours |
| 03h, 04h | the day number, low and high byte: days since 31 Dec 1977 (CP/M 3's) |
| 05h | the day of the month |
| 06h | the month, 00h-0Bh (January is 00h) |
| 07h | the year - 1900 (2026 is 7Eh) |

In BCD, or in binary after `OUT 41h` with FFh (which toggles it; `IN 41h`
reads 01h for binary). The day number is always binary.

`OUT 42h` (this firmware's) sets the selected register: 00h-02h (BCD or
binary, as read) replace that field of the current time; a write to 03h is
kept for the next write to 04h, which sets the day number from both (the
time of day kept). Values out of range, and registers 05h-07h, are ignored.
CP/M 3's BIOS `TIME` (and so `DATE SET`) writes the day number, then hours,
minutes, seconds. The time is the board's real-time clock where it has one;
see [Clock](../terminal/clock.md).

## Timer (43h)

picosim's 60 Hz timer interrupt: `OUT 43h` with 01h starts it, with 00h
stops it, and `IN 43h` reads 01h while it runs.

## Millisecond tick (44h, 45h)

A free-running millisecond count, for timing without interrupts (the CPU
has no fixed speed): `IN 44h` reads its low byte and latches the high byte
for `IN 45h`. It wraps every 65.536 s.

```
        IN   A,(44H)    ; low byte, latches the high
        LD   L,A
        IN   A,(45H)    ; high byte
        LD   H,A        ; HL = milliseconds
```

## Hardware control (A0h)

Locked until AAh is written; then one command: bit 7 stops the machine
(`BYE.COM`), bit 6 resets it (`RESET.COM`). `IN A0h` reads FFh while
locked, 00h when unlocked.

## The AY-3-8910 (D8h, D0h)

As on Ed Brindley's RC2014 YM2149 board: `OUT D8h` selects a register,
`OUT D0h` writes it, `IN D0h` reads it. Registers 00h-0Fh are the chip's;
clocked at 1.8432 MHz, so a tone of f Hz has period 115200 / f (decimal).

Register 10h is this firmware's (a real AY ignores it): the stereo layout.
Write 00h for SET-UP's Stereo setting, 01h mono, 02h ABC, 03h ACB. Reading
it gives what was written in bits 0-1 and the layout in use (01h-03h) in
bits 4-5, so FFh (older firmware, or a real chip) means no stereo. A
machine reset writes 00h to registers 00h-10h: silence, and the layout back
to SET-UP's.

Without a DAC (or with Sound DAC set to not fitted) the ports read FFh, as
with no chip. To find the chip, write FFh to register 01h and read it back:
the chip keeps four bits there, so it reads 0Fh (AYTEST also checks
register 02h with 5Ah).
