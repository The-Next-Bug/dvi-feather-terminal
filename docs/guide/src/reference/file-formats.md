# File formats

## Disk images (.DSK)

Raw images of 128-byte sectors, track by track from track 0, sector 1; the
format is told by the file's size (see
[Drives and disk images](../cpm/drives.md)). They are ordinary CP/M file
systems: with cpmtools,

```sh
cpmls -f ibm-3740 CPM22.DSK        # 8" floppy
cpmls -f z80pack-hd HD4.DSK        # 4 MB
cpmls -f dvi-hd8 CPM32.DSK         # 8 MB, from docs/cpmtools-diskdefs
cpmcp -f ibm-3740 CPM22.DSK notes.txt 0:NOTES.TXT
```

## DRIVES.CFG and drive sets (.SET)

`/DISKS80/DRIVES.CFG` and `/DISKS80/SETS/NAME.SET` are text, one line a
drive: the drive letter, `=`, and the image's name without `.DSK`. Drives
left out are empty; `RAMDISK` names the RAM disk.

```
A=CPM22
B=Z80TESTS
```

## PSG

The tunes [AYPLAY](../cpm/tools/ayplay.md) plays: the AY's register writes
frame by frame, as emulators record them.

| Bytes | Meaning |
|---|---|
| `50h 53h 47h 1Ah` | `PSG` and ^Z |
| 12 bytes | the rest of the header (ignored) |
| `r v` (r 00h-0Fh) | write v to register r (see [the AY's registers](ports.md#the-ay-3-8910-d8h-d0h)) |
| `FFh` | end of a frame (20 ms) |
| `FEh n` | n x 4 empty frames |
| `FDh` | end of the tune |

## AYT

The songs [AYTRACK](../cpm/tools/aytrack.md) saves: its song memory as it
is, 26 records (3328 bytes). Offsets in decimal and hex:

| Offset | Size | Contents |
|---|---|---|
| 0 (000h) | 4 | `AYT1` |
| 4 (004h) | 1 | speed: frames (20 ms) a row |
| 5 (005h) | 1 | order count: entries used in the order list, 1-64 |
| 6 (006h) | 64 | the order list: a pattern number (0-15) each |
| 70 (046h) | 8 x 18 | the instruments |
| 214 (0D6h) | 16 x 192 | the patterns |
| 3286 (CD6h) | 42 | unused |

An instrument (18 bytes): 16 volumes (0-15), one a frame, the last held;
a mode (bit 0 tone, bit 1 noise); a noise period (0-31).

A pattern (192 bytes): 32 rows of 6 bytes, one pair per channel A, B, C:

| Byte | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| | A note | A instrument | B note | B instrument | C note | C instrument |

A note is 12-95 (C1-B7), octave x 12 + semitone (C = 0 ... B = 11; 48 is C4), 0
empty, FFh off; the instrument is 0-7.

## USBLOG.TXT

Text with CR LF line ends, each line the seconds since power-on and an
event (see [Troubleshooting](../terminal/troubleshooting.md#the-usb-log)).
Each power-on starts with a line giving the firmware and the date.

## FIRMWARE.UF2

A UF2 file for the RP2350 (family `rp2350-arm-s`) with the blocks in order,
as `picotool` and the build write them; see
[Firmware updates](../terminal/firmware-update.md).

## ZIMODEM.ZMI

The [WiFi modem](../terminal/wifi-modem.md)'s firmware with what it is
for, so that the terminal can refuse a wrong file before it erases
anything (flashing the module from the terminal is still to come).
`scripts/build-zimodem.sh` writes it with `src/modem/zmi_gen.py`, and the
release ZIP has it in `modem/`; `src/modem/zmi.c` reads it. Little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `ZMI1` |
| 4 | 2 | header size: 64, or more (a later version's fields, skipped) |
| 6 | 1 | chip: 1 ESP8266 |
| 7 | 1 | 0 |
| 8 | 16 | module, NUL-padded (`esp12f`) |
| 24 | 24 | Zimodem's version as `ATI4` reports it, NUL-padded |
| 48 | 4 | the flash size the image is built for, in bytes |
| 52 | 4 | parts (1-16) |
| 56 | 4 | bytes of parts, from the header's end to the CRC |
| 60 | 4 | 0 |

Then each part: its flash address (4 bytes; a multiple of 4 KB, ascending,
not overlapping, inside the flash), its length (4 bytes, not 0) and its
data; then a CRC-32 (as zlib's) of every byte before it. An ESP8266
container has an ESP image at address 0: `E9h` first, and the flash size
in its header (byte 3, high nibble) is the container's.

The checks before flashing: the header's fields, the parts, the ESP
image, the CRC, and that the chip, the module's name and its flash size
are the board's.
