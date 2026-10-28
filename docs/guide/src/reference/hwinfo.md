# Hardware information (port 12)

What the machine runs on, for [HWINFO](../cpm/tools/hwinfo.md) and any
other program (`src/machine/machine.h`; the records are built by
`apps/terminal/hwinfo_glue.c`).

## Protocol

`OUT 12` an item number; `IN 12` then reads the item's bytes in turn, and 0
after its end (at most 128 bytes). Without the device (older firmware,
z80pack's picosim) `IN 12` reads FFh: check item 0 first.

- **Item 0** reads `H`, `W` and the protocol version (1).
- **Items 1 and 2** are text ending in 0: the firmware's version and build
  date, and the board (its header's name, `boards/<name>.h`).
- **Items 16 and up** are one piece of hardware each, at a fixed number,
  so a program can look one up: a status byte, then its name and a
  description, each text ending in 0. Status 0 means no record under that
  number: the numbers in between may be used later, so read them all.

## Items

{{#include ../generated/hwinfo-items.md}}

Hardware the board does not have gives no record (status 0). Hardware the
board header names but this firmware does not drive yet is listed as
planned. Items 29-31 once listed hardware that was dropped; they stay
unused, as older programs may know them.

## Example: is there a real-time clock?

```
        LD   A,0          ; item 0: 'H' 'W' version?
        OUT  (12),A
        IN   A,(12)
        CP   'H'
        JR   NZ,NODEVICE
        LD   A,26         ; item 26: the real-time clock
        OUT  (12),A
        IN   A,(12)       ; its status
        CP   1            ; 1: there and in use
        JR   Z,HASRTC
```
