# Hardware information (port 0Ch)

What the machine runs on, for [HWINFO](../cpm/tools/hwinfo.md) and any
other program (`src/machine/machine.h`; the records are built by
`apps/terminal/hwinfo_glue.c`).

## Protocol

`OUT 0Ch` an item number; `IN 0Ch` then reads the item's bytes in turn, and
00h after its end (at most 128 bytes). Without the device (older firmware,
z80pack's picosim) `IN 0Ch` reads FFh: check item 00h first. Ports and
values are hex, as on the [Z80 I/O ports](ports.md) page; the items are
listed with their decimal numbers too, which HWINFO's sources use.

- **Item 00h** reads `H`, `W` and the protocol version (01h).
- **Items 01h and 02h** are text ending in 00h: the firmware's version and build
  date, and the board (its header's name, `boards/<name>.h`).
- **Items 10h (16) and up** are one piece of hardware each, at a fixed number,
  so a program can look one up: a status byte, then its name and a
  description, each text ending in 00h. Status 00h means no record under that
  number: the numbers in between may be used later, so read them all.

An `OUT 0Ch` always starts the item from its first byte; it can be read
again from the start the same way. Items do not change while they are
read, but they can change between reads (a stick plugged in, the time
set).

## Data

Text is ASCII: at most 127 characters, then 00h. A record:

| Offset | Size | Field |
|---|---|---|
| +0 | 1 | the status (below) |
| +1 | n + 1 | the name, then 00h, e.g. `Z80 memory` |
| +n + 2 | m + 1 | the description, then 00h: free text, may be long (a program should wrap it) |
| after it | | 00h for every further `IN` |

The whole record is at most 128 bytes. An item without a record reads 00h
at once, which is status 00h.

## Items

{{#include ../generated/hwinfo-items.md}}

Hardware the board does not have gives no record (status 00h). Hardware the
board header names but this firmware does not drive yet is listed as
planned. Items 1Dh-1Fh (29-31) once listed hardware that was dropped; they stay
unused, as older programs may know them.

## Examples

### Is there a real-time clock?

```
        LD   A,00H        ; item 00h: 'H' 'W' version?
        OUT  (0CH),A
        IN   A,(0CH)
        CP   'H'
        JR   NZ,NODEVICE
        LD   A,1AH        ; item 1Ah (26): the real-time clock
        OUT  (0CH),A
        IN   A,(0CH)     ; its status
        CP   01H          ; 01h: there and in use
        JR   Z,HASRTC
```

### Printing the firmware's version (CP/M)

```
        LD   A,01H        ; item 01h: the firmware's version and date
        OUT  (0CH),A
NEXT:   IN   A,(0CH)
        OR   A            ; 00h: its end
        RET  Z
        CP   0FFH         ; FFh: no device here
        RET  Z
        LD   E,A
        LD   C,02H        ; BDOS 2: console output
        CALL 0005H
        JR   NEXT
```

### Every record

```
        LD   B,10H        ; the first record
ITEM:   LD   A,B
        OUT  (0CH),A
        IN   A,(0CH)      ; the status
        OR   A
        JR   Z,SKIP       ; 00h: no record under this number
        ...               ; then IN 0Ch: the name up to 00h,
        ...               ; then the description up to 00h
SKIP:   INC  B
        JR   NZ,ITEM      ; up to item FFh
```

[HWINFO](../cpm/tools/hwinfo.md) (`src/machine/cpm/hwinfo.asm`) does this
and wraps the descriptions to the screen.
