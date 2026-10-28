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

## Items

{{#include ../generated/hwinfo-items.md}}

Hardware the board does not have gives no record (status 00h). Hardware the
board header names but this firmware does not drive yet is listed as
planned. Items 1Dh-1Fh (29-31) once listed hardware that was dropped; they stay
unused, as older programs may know them.

## Example: is there a real-time clock?

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
