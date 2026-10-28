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
| `r v` (r 00h-0Fh) | write v to register r |
| `FFh` | end of a frame (20 ms) |
| `FEh n` | n x 4 empty frames |
| `FDh` | end of the tune |

## AYT

The songs [AYTRACK](../cpm/tools/aytrack.md) saves, 26 records (3328 bytes):

| Part | Size | Contents |
|---|---|---|
| header | 4 | `AYT1` |
| speed | 1 | frames a row |
| order count | 1 | entries in the order list |
| orders | 64 | pattern numbers |
| instruments | 8 x 18 | each: 16 volumes, a mode (bit 0 tone, bit 1 noise), a noise period |
| patterns | 16 x 32 rows x 3 channels x 2 | each cell: a note (12-95: octave x 12 + semitone; 0 empty, FFh off) and an instrument |

## USBLOG.TXT

Text with CR LF line ends, each line the seconds since power-on and an
event (see [Troubleshooting](../terminal/troubleshooting.md#the-usb-log)).
Each power-on starts with a line giving the firmware and the date.

## FIRMWARE.UF2

A UF2 file for the RP2350 (family `rp2350-arm-s`) with the blocks in order,
as `picotool` and the build write them; see
[Firmware updates](../terminal/firmware-update.md).
