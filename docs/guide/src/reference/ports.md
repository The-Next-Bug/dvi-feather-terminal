# Z80 I/O ports

The machine is z80pack's picosim, with devices of this firmware's own
added (`src/machine/machine.c`, `machine.h`). Unlisted ports read FFh and
ignore writes.

Port numbers and the values written to and read from them are hex
throughout this page, marked with `h` (in the assembler examples, `H`, as
z80asm writes it): port `0Ah` is port 10 in decimal. Sizes and counts
(48 KB, 15 banks) are decimal. Bit 7 is the most significant bit of a
byte, bit 0 the least; 16-bit values are little-endian (low byte first),
as the Z80 stores them. Where the address space matters, see the
[memory maps](memory-map.md).

Every device answers at once: there are no wait states and no busy bits
that ever stay set, so a program can write a command and read its status
on the next instruction.

| Port | Direction | Device | Origin |
|---|---|---|---|
| 00h | in | console status | picosim |
| 00h | out | (picosim's LED: ignored) | picosim |
| 01h | in, out | console data | picosim |
| 02h | in | second serial port status | picosim |
| 03h | in, out | second serial port data (the board's UART at the SET-UP line settings) | picosim |
| 04h | in, out | disk controller: status, command | picosim |
| 05h | in | printer status (none fitted) | picosim |
| 06h | in, out | printer data | picosim |
| 08h | in, out | the disk in a drive | this firmware |
| 0Ah | in, out | file transfer device | this firmware |
| 0Ch | in, out | hardware information | this firmware |
| 40h | in, out | MMU | picosim |
| 41h | in, out | clock: format, register select | picosim |
| 42h | in, out | clock: data | picosim; setting it is this firmware's |
| 43h | in, out | 60 Hz timer interrupt | picosim |
| 44h, 45h | in | millisecond tick: low, high | this firmware |
| A0h | in, out | hardware control | picosim |
| D8h | out | AY-3-8910 register select | RC2014's |
| D0h | in, out | AY-3-8910 data | RC2014's |
| FEh, FFh | in | front panel switches: read 00h (writes ignored) | picosim |

## Console (00h, 01h)

The console is the [terminal](vt100.md): programs send it escape sequences
and read its replies (cursor position reports and the like) as keys.

**`IN 00h`: status**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | TXBUSY | 0 | 0 | 0 | 0 | 0 | 0 | RXEMPTY |

- **RXEMPTY** (bit 0): **0 when a key is waiting**, 1 when none. Note the
  sense: it is inverted from what many machines use.
- **TXBUSY** (bit 7): 0 when a character can be written. Always 0 here;
  test it anyway if the code should also run on other picosim machines.
- Bits 1-6 read 0.

**`IN 01h`: the key.** The next byte of the keyboard queue (64 bytes;
replies from the terminal arrive through it too). With no key waiting it
returns the last key again, so check RXEMPTY first.

**`OUT 01h`: a character to the terminal.** Bit 7 is cleared, as picosim
does (some CP/M software sets it), so the console carries 7-bit ASCII and
escape sequences only: Latin-1 and the glyphs at 80h-FFh of the
[character set](charset.md) cannot be printed from the Z80.

```
CONST:  IN   A,(00H)    ; A = 00h no key, FFh a key waiting
        CPL
        AND  01H
        RET  Z
        LD   A,0FFH
        RET

CONIN:  IN   A,(00H)    ; wait for a key, return it in A
        RRCA            ; RXEMPTY into carry
        JR   C,CONIN
        IN   A,(01H)
        RET

CONOUT: IN   A,(00H)    ; write C
        RLCA            ; TXBUSY into carry
        JR   C,CONOUT
        LD   A,C
        OUT  (01H),A
        RET
```

## Second serial port (02h, 03h)

The board's UART: the host connected to the serial port (see
[Connecting a host](../start/host.md)), at SET-UP's line settings and flow
control.

**`IN 02h`: status**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | TXBUSY | 0 | 0 | 0 | 0 | 0 | 0 | RXEMPTY |

The same layout as the console's: 00h when a byte has arrived, 01h when
none. `IN 03h` reads the byte (the last one again when none is waiting);
`OUT 03h` sends one, all 8 bits. Writes to port 02h are ignored. The
console routines above work with 02h/03h in place of 00h/01h.

## Printer (05h, 06h)

No printer is fitted: `IN 05h` reads 00h (not ready; picosim reads FFh
with a printer), `IN 06h` reads 1Ah (^Z), and `OUT 06h` is discarded.

## Disk controller (04h)

picosim's SD-FDC (`lib/z80pack/iodevices/sd-fdc.c`): a DMA controller on
one port that reads or writes one 128-byte sector per command. The disks
are the images mounted on drives A-H (see
[Drives and disk images](../cpm/drives.md)).

### The command byte (`OUT 04h`)

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | CMD | CMD | CMD | CMD | DRIVE | DRIVE | DRIVE | DRIVE |

| Command | Name | Action |
|---|---|---|
| `10h` | SETDD | the next two `OUT 04h`s are the address of the disk descriptor, low byte then high (the drive bits are ignored) |
| `20h` + drive | READ | read the sector the descriptor names from the drive into memory at its DMA address |
| `40h` + drive | WRITE | write the sector from memory at the DMA address to the drive |

DRIVE is 0-7 for A-H (8-15 answer status 01h). Any other command is
ignored. After SETDD the controller takes the next two bytes written to
port 04h as the address, whatever they are.

### The disk descriptor

Four bytes anywhere in memory, set once with SETDD and read by the
controller at every READ and WRITE, so a program changes the bytes between
commands and never needs SETDD again:

| Offset | Size | Field | Values |
|---|---|---|---|
| +0 | 1 | track | 00h-FFh, counted from 0 |
| +1 | 1 | sector | counted from **1** |
| +2 | 2 | DMA address | the 128 bytes' address in Z80 memory, low byte first |

The descriptor and the data are both read and written through the MMU: at
0000h-BFFFh in the bank selected when the command is written, at
C000h-FFFFh in common memory (see [Memory maps](memory-map.md)). The
controller does not wrap past FFFFh: a DMA address above FF80h is refused
(status 07h), and sector data read into the boot ROM's page (FF00h-FFFFh)
is lost, as all writes there are.

### Status (`IN 04h`)

Every `OUT 04h` first sets the status to 00h; `IN 04h` reads the outcome
of the last command, which has finished by then (the transfer happens
during the `OUT`).

| Status | Meaning | Here |
|---|---|---|
| 00h | OK | |
| 01h | no such drive | DRIVE 8-15 |
| 02h | no disk | nothing mounted, or a hard-disk image the system has not asked port 08h about (below) |
| 03h | seek error | the card could not seek in the image |
| 04h | read error | the card failed |
| 05h | write error | the card failed or is full |
| 06h | DMA error | (not used here) |
| 07h | DMA address out of range | above FF80h |
| 08h | track out of range | track ≥ the format's tracks |
| 09h | sector out of range | sector 0, or above the format's sectors per track |

### Geometry

Sectors are 128 bytes. Port 08h tells which format a drive holds:

| Format (`IN 08h`) | Tracks | Sectors per track | Track numbers | Sector numbers | Image size (bytes) |
|---|---|---|---|---|---|
| 01h 8" floppy (SSSD, IBM 3740) | 77 | 26 | 0-76 | 1-26 | 256,256 |
| 02h 4 MB hard disk (z80pack's) | 255 | 128 | 0-254 | 1-128 | 4,177,920 |
| 03h 8 MB hard disk | 256 | 255 | 0-255 | 1-255 | 8,355,840 |

The controller does no translation: sector *s* of track *t* is at byte
(*t* × sectors per track + *s* − 1) × 128 of the image. The interleave of
CP/M's 8" format (skew 6) is applied by the BIOS (its SECTRAN, see
[Writing a BIOS](bios.md)), not by the controller; the hard disks have
none.

### Hard disks need asking

A 4 MB or 8 MB image is refused (status 02h, no disk) on a drive the
running system has not asked about on port 08h since the last reset, so a
BIOS that only knows 8" floppies cannot read a hard disk as one and write
over it. Read the drive's format on port 08h once before using it.

### Example: the boot ROM

The 44-byte boot ROM at FF00h (picosim's `bootrom.asm`) is the whole
protocol in one piece: it reads track 0, sector 1 of drive A to 0000h and
jumps there.

```
        DI
        XOR  A
        LD   (0F000H),A     ; descriptor at F000h: track 0
        LD   (0F002H),A     ; DMA address 0000h
        LD   (0F003H),A
        LD   A,01H
        LD   (0F001H),A     ; sector 1
        LD   A,10H          ; SETDD
        OUT  (04H),A
        LD   A,00H          ; the descriptor's address, low
        OUT  (04H),A
        LD   A,0F0H         ; and high
        OUT  (04H),A
        LD   A,20H          ; READ, drive 0 (A)
        OUT  (04H),A
        IN   A,(04H)        ; status
        OR   A
        JP   Z,0000H        ; OK: run the boot sector
        CP   02H
        JP   Z,0000H        ; (no disk: as picosim's)
        HALT                ; any other error stops the machine
```

### Example: read and write a sector

```
; SETUP once; then RDSEC or WRSEC per sector.
; In:  A = drive (00h-07h), E = track, D = sector (from 1),
;      HL = the 128-byte buffer.
; Out: A = status, Z set when 00h (OK).

FDCDD:  DS   4              ; the descriptor: track, sector, DMA low, high

SETUP:  LD   A,10H          ; SETDD
        OUT  (04H),A
        LD   HL,FDCDD
        LD   A,L
        OUT  (04H),A
        LD   A,H
        OUT  (04H),A
        RET

RDSEC:  LD   B,20H          ; READ
        JR   DOSEC
WRSEC:  LD   B,40H          ; WRITE
DOSEC:  LD   (FDCDD),DE     ; track (E) at +0, sector (D) at +1
        LD   (FDCDD+2),HL   ; DMA address
        OR   B              ; command + drive
        OUT  (04H),A
        IN   A,(04H)
        OR   A
        RET
```

For instance, the first directory sector of an 8" disk in drive B
(track 2, sector 1):

```
        CALL SETUP
        LD   A,01H          ; drive B
        LD   E,2            ; track 2 (after the two system tracks)
        LD   D,1            ; sector 1
        LD   HL,BUF
        CALL RDSEC
        JR   NZ,ERROR
```

### Under CP/M

Both BIOSes give SETDD once, at cold boot, and keep their own descriptor
(CP/M 2.2's at 0040h-0043h in page zero; CP/M 3's inside its BIOS). A
program that sends SETDD itself leaves the BIOS's I/O going to its own
descriptor, so it must not return to CP/M without a cold boot. Under CP/M
2.2 a program can instead fill the BIOS's descriptor at 0040h and send only
READ/WRITE. Writing behind the BDOS's back to a drive it has logged in can
corrupt that disk: let it log the drive in again (^C) afterwards. Prefer
the BIOS's SELDSK/SETTRK/SETSEC/SETDMA/READ/WRITE where they will do.

## Drive format (08h)

`OUT 08h` selects a drive (00h-07h for A-H); `IN 08h` then reads its
format, and every further `IN 08h` the number of images mounted on that
drive since power-on (modulo 256), so a BIOS can tell a changed disk even
when it has the same format.

| Format | Disk |
|---|---|
| 00h | none (also for drives 08h and up) |
| 01h | 8" floppy |
| 02h | 4 MB hard disk |
| 03h | 8 MB hard disk |

Reading the format also lets the disk controller use a hard disk on that
drive until the next reset.

```
        LD   A,01H          ; drive B
        OUT  (08H),A
        IN   A,(08H)        ; its format, 00h-03h
        LD   B,A
        IN   A,(08H)        ; images mounted on it so far
        LD   C,A
```

See [Writing a BIOS](bios.md) for the disk parameter blocks that go with
each format.

## File transfer (0Ah)

Files in `/XFER` on the SD card or a USB stick, for `XLS`, `XPIP` and
`XERA`. See [File transfer device](xfer.md).

## Hardware information (0Ch)

What the machine runs on, for `HWINFO`. See [Hardware information](hwinfo.md).

## MMU (40h)

`OUT 40h` selects a bank: 00h is the 64 KB bank 0; 01h and up replace
0000h-BFFFh with a 48 KB bank, while C000h-FFFFh (bank 0's top 16 KB,
including the boot ROM) stays in place, common to all. See the
[memory maps](memory-map.md#the-z80s-address-space).

**`IN 40h`**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | BANKS | BANKS | BANKS | BANKS | SEL | SEL | SEL | SEL |

- **BANKS** (bits 4-7): how many banks there are besides bank 0, 0-15.
  Most boards have 2 (in SRAM); the PSRAM boards have 15 (in the PSRAM).
- **SEL** (bits 0-3): the bank selected.

Selecting a bank that is not fitted stops the machine (`MMU: no bank n`).
A reset selects bank 0. Code that switches banks must run, and keep its
stack, in common memory (C000h and up), and disable interrupts unless
every bank has the handler at 0038h.

```
        IN   A,(40H)        ; how many banks besides bank 0?
        RRCA
        RRCA
        RRCA
        RRCA
        AND  0FH            ; A = 0-15
```

Copying a byte from bank 2 into common memory, from code at C000h or
above:

```
        DI
        LD   A,02H
        OUT  (40H),A        ; bank 2 at 0000h-BFFFh
        LD   A,(1234H)      ; bank 2's byte
        LD   (SAVE),A       ; SAVE in common memory
        XOR  A
        OUT  (40H),A        ; bank 0 back
        EI
```

## Clock (41h, 42h)

`OUT 41h` selects a register, `IN 42h` reads it:

| Register | Value | Range (binary) |
|---|---|---|
| 00h | seconds | 0-59 |
| 01h | minutes | 0-59 |
| 02h | hours | 0-23 |
| 03h | the day number, low byte | |
| 04h | the day number, high byte | days since 31 Dec 1977 (CP/M 3's: 1 Jan 1978 is day 1) |
| 05h | the day of the month | 1-31 |
| 06h | the month | 0-11 (January is 00h) |
| 07h | the year - 1900 | 2026 is 126 (7Eh) |

Values are BCD (23 reads 23h) until `OUT 41h` with FFh, which toggles
binary; the day number is always binary. A reset leaves the format as it
was.

**`IN 41h`**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | 0 | 0 | 0 | 0 | 0 | 0 | 0 | BIN |

**BIN**: 1 binary, 0 BCD.

`OUT 42h` (this firmware's) sets the selected register: 00h-02h (BCD or
binary, as read) replace that field of the current time, the fraction of
the second zeroed; a write to 03h is kept for the next write to 04h, which
sets the day number from both (the time of day kept). Values out of range,
and registers 05h-07h, are ignored. CP/M 3's BIOS `TIME` (and so `DATE
SET`) writes the day number, then hours, minutes, seconds. The time is the
board's real-time clock where it has one; see [Clock](../terminal/clock.md).

The registers are read one at a time, so a second can tick over between
them: read the seconds first and again last, and start over if they
differ.

```
GETTIM: LD   A,00H          ; HL = minutes:hours, B = seconds (BCD)
        OUT  (41H),A
        IN   A,(42H)
        LD   B,A            ; seconds
        LD   A,01H
        OUT  (41H),A
        IN   A,(42H)
        LD   L,A            ; minutes
        LD   A,02H
        OUT  (41H),A
        IN   A,(42H)
        LD   H,A            ; hours
        LD   A,00H
        OUT  (41H),A
        IN   A,(42H)
        CP   B              ; the same second?
        JR   NZ,GETTIM
        RET
```

Setting the time to 12:30:00 (BCD):

```
        LD   A,02H          ; hours
        OUT  (41H),A
        LD   A,12H
        OUT  (42H),A
        LD   A,01H          ; minutes
        OUT  (41H),A
        LD   A,30H
        OUT  (42H),A
        LD   A,00H          ; seconds
        OUT  (41H),A
        XOR  A
        OUT  (42H),A
```

## Timer (43h)

picosim's 60 Hz timer interrupt: `OUT 43h` with 01h starts it, any other
value stops it, and `IN 43h` reads 01h while it runs, 00h when stopped. A
reset stops it.

Each tick (every 16.667 ms) raises a maskable interrupt with the data byte
FFh: in interrupt mode 0 that is `RST 38H`, in mode 1 the CPU calls 0038h
anyway, in mode 2 FFh is the low byte of the vector table entry
(I × 256 + FFh). Nothing needs acknowledging: accepting the interrupt
clears the request. A tick raised while interrupts are disabled waits for
`EI`, but ticks are not counted, so several missed ones arrive as one.
`HALT` with interrupts enabled waits for the next tick (with interrupts
disabled it stops the machine).

```
        DI
        IM   1
        LD   A,0C3H         ; JP TICK at 0038h
        LD   (0038H),A
        LD   HL,TICK
        LD   (0039H),HL
        LD   A,01H
        OUT  (43H),A        ; start the timer
        EI
        ...

TICK:   PUSH AF
        PUSH HL
        LD   HL,(COUNT)     ; count the ticks
        INC  HL
        LD   (COUNT),HL
        POP  HL
        POP  AF
        EI
        RETI
```

Under CP/M 2.2, 0038h is also the breakpoint vector of DDT and similar
debuggers.

## Millisecond tick (44h, 45h)

A free-running millisecond count, for timing without interrupts (the CPU
has no fixed speed): `IN 44h` reads its low byte and latches the high byte
for `IN 45h`. Always read 44h first. The count is 16 bits and wraps every
65.536 s.

```
        IN   A,(44H)        ; low byte, latches the high
        LD   L,A
        IN   A,(45H)        ; high byte
        LD   H,A            ; HL = milliseconds
```

Waiting DE milliseconds (1-32767), correct across the wrap:

```
DELAY:  CALL TICKS
        ADD  HL,DE
        EX   DE,HL          ; DE = the end
WAIT:   CALL TICKS
        OR   A
        SBC  HL,DE          ; now - end
        BIT  7,H            ; still negative: not yet
        JR   NZ,WAIT
        RET

TICKS:  IN   A,(44H)
        LD   L,A
        IN   A,(45H)
        LD   H,A
        RET
```

## Hardware control (A0h)

Locked until AAh is written; then the next write is one command, and the
port locks again:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | HALT | RESET | – | – | – | – | – | – |

- **HALT** (bit 7): stop the machine (`BYE.COM`): the monitor shows
  `System halted`.
- **RESET** (bit 6): reset it (`RESET.COM`), as the reset key F11 does.
  HALT wins when both are set.
- Bits 5 and 4 (picosim's CPU switches) and 0-3 do nothing.

`IN A0h` reads FFh while locked, 00h when unlocked.

```
        LD   A,0AAH         ; unlock
        OUT  (0A0H),A
        LD   A,40H          ; reset
        OUT  (0A0H),A
```

## The AY-3-8910 (D8h, D0h)

As on Ed Brindley's RC2014 YM2149 board: `OUT D8h` selects a register,
`OUT D0h` writes it, `IN D0h` reads it. The chip is clocked at
1.8432 MHz. Registers 00h-0Fh are the chip's; 10h is this firmware's.

| Register | Bits used | What |
|---|---|---|
| 00h, 01h | 8, 4 | channel A tone period, fine and coarse: 12 bits |
| 02h, 03h | 8, 4 | channel B tone period |
| 04h, 05h | 8, 4 | channel C tone period |
| 06h | 5 | noise period |
| 07h | 8 | mixer (below) |
| 08h, 09h, 0Ah | 5 | channel A, B, C level (below) |
| 0Bh, 0Ch | 8, 8 | envelope period, fine and coarse: 16 bits |
| 0Dh | 4 | envelope shape (below); writing it restarts the envelope |
| 0Eh, 0Fh | 8 | the chip's I/O ports A and B: kept and read back, connected to nothing |
| 10h | 2 | stereo layout (this firmware's, below) |

Unused bits read back as 0 (write FFh to register 01h, read 0Fh).

**Frequencies** (periods in decimal; a period of 0 counts as 1):

- tone: f = 1,843,200 / (16 × period) = 115200 / period Hz, so A4
  (440 Hz) is period 262 (0106h);
- noise: 115200 / period Hz, period 1-31;
- envelope: one step of its 16 every 16 × period / 1,843,200 s, one ramp
  every 256 × period / 1,843,200 s (period 7200, 1C20h, is 1 s).

**Register 07h: mixer** (a 0 bit turns the source **on**)

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | IOB | IOA | NOISE C | NOISE B | NOISE A | TONE C | TONE B | TONE A |

3Eh is tone on A only; 38h tones on A, B and C; 3Fh everything off. IOA
and IOB (the I/O ports' directions) do nothing here.

**Registers 08h-0Ah: level**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | – | – | – | M | L3 | L2 | L1 | L0 |

L3-L0: the level, 0 (silent) to 15. M = 1: follow the envelope instead.

**Register 0Dh: envelope shape**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | – | – | – | – | CONT | ATT | ALT | HOLD |

| Shape | Envelope |
|---|---|
| 00h-03h, 09h | `\___` decay once, then silent |
| 04h-07h, 0Fh | `/___` attack once, then silent |
| 08h | `\\\\` decay, repeated |
| 0Ah | `\/\/` decay and attack, repeated |
| 0Bh | `\‾‾‾` decay once, then hold full |
| 0Ch | `////` attack, repeated |
| 0Dh | `/‾‾‾` attack once, then hold full |
| 0Eh | `/\/\` attack and decay, repeated |

**Register 10h: stereo** (a real AY ignores it)

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| write | – | – | – | – | – | – | SET | SET |
| read | 0 | 0 | USE | USE | 0 | 0 | SET | SET |

- **SET** (bits 0-1): what was written: 0 SET-UP's Stereo setting, 1 mono
  (A, B and C in both ears), 2 ABC (A left, B center, C right), 3 ACB (A
  left, C center, B right).
- **USE** (bits 4-5, read only): the layout in use, 1-3 as above.

So reading FFh (older firmware, or a real chip) means no stereo.

A machine reset writes 00h to registers 00h-10h: silence, and the layout
back to SET-UP's. Without a DAC (or with Sound DAC set to not fitted) the
ports read FFh, as with no chip. To find the chip, write FFh to register
01h and read it back: it keeps four bits there, so it reads 0Fh (AYTEST
also checks register 02h with 5Ah).

```
AYW:    LD   A,D            ; write E to register D
        OUT  (0D8H),A
        LD   A,E
        OUT  (0D0H),A
        RET

        LD   DE,0006H       ; A4 on channel A: period 262 = 0106h
        CALL AYW
        LD   DE,0101H
        CALL AYW
        LD   DE,073EH       ; mixer: tone A only
        CALL AYW
        LD   DE,080FH       ; channel A at level 15
        CALL AYW
        ...
        LD   DE,0800H       ; silence channel A
        CALL AYW
```

[AYTEST](../cpm/tools/aytest.md), [AYPLAY](../cpm/tools/ayplay.md) and
[F83's AY.BLK](../cpm/f83.md#the-sound-chip-ayblk) are fuller examples.
