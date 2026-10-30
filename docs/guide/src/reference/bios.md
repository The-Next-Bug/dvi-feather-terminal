# Writing a BIOS

Notes for writing a CP/M BIOS for this machine, from the two in the source:
CP/M 2.2's `src/machine/cpm/bios.asm` and CP/M 3's banked
`src/machine/cpm/cpm3/bnkbios3.asm` (both started from picosim's).

## Booting

At reset picosim's boot ROM loads track 0, sector 1 of drive A to 0000H and
runs it; that boot sector (`src/machine/cpm/boot.asm`) loads the system
from the system tracks. Drive A must be an 8" floppy image. CP/M 2.2's
warm boot reloads the CCP and BDOS from the system tracks of drive A.

## Disks

- Use the [disk controller](ports.md#disk-controller-04h) on port 04h:
  128-byte sectors by DMA, the sector number 1-based.
- Ask [port 08h](ports.md#drive-format-08h) what each drive holds and pick
  the disk parameter block (DPB) to match, at SELDSK (CP/M 2.2) or at
  log-in (CP/M 3). Until a system has asked about a drive, the controller
  refuses it a hard-disk image.
- The formats, as CP/M sees them:

| DPB field | 8" floppy | 4 MB hard disk | 8 MB hard disk |
|---|---|---|---|
| SPT (sectors per track) | 26 | 128 | 255 |
| BSH, BLM (block size) | 3, 7 (1 KB) | 4, 15 (2 KB) | 5, 31 (4 KB) |
| EXM | 0 | 0 | 1 |
| DSM (blocks - 1) | 242 | 2039 | 2039 |
| DRM (directory entries - 1) | 63 | 1023 | 1023 |
| AL0, AL1 | C0h, 00h | FFh, FFh | FFh, 00h |
| CKS | 16 | 0 | 0 |
| OFF (reserved tracks) | 2 | 0 | 0 |

These are `bios.asm`'s DPBs; the images on the card were written with them,
so another BIOS must use the same.

- **Sector skew**: the 8" format is skewed by 6, as on IBM 3740 disks; the
  controller does none, so the BIOS's SECTRAN maps CP/M's logical sector
  (from 0) to the sector given to the controller (from 1) with this table.
  The hard disks have no skew: SECTRAN returns the sector + 1 (XLT 0).

| Logical | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical | 1 | 7 | 13 | 19 | 25 | 5 | 11 | 17 | 23 | 3 | 9 | 15 | 21 |

| Logical | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical | 2 | 8 | 14 | 20 | 26 | 6 | 12 | 18 | 24 | 4 | 10 | 16 | 22 |

- **The descriptor**: give the controller's SETDD once, at cold boot, for a
  4-byte descriptor of the BIOS's own (CP/M 2.2's is at 0040h in page zero),
  and let SETTRK, SETSEC and SETDMA fill it in. READ and WRITE then need
  one `OUT` and one `IN` each. From `bios.asm`, shortened (8080 mnemonics, as the CP/M
  2.2 BIOS is written):

```
SETTRK  MOV  A,C            ;track in C
        STA  FDCCMD+DDTRK   ;descriptor +0
        RET
SETSEC  MOV  A,C            ;sector in C, from SECTRAN (from 1)
        STA  FDCCMD+DDSEC   ;descriptor +1
        RET
SETDMA  MOV  A,C            ;DMA address in BC
        STA  FDCCMD+DDLDMA  ;descriptor +2
        MOV  A,B
        STA  FDCCMD+DDHDMA  ;descriptor +3
        RET
READ    LDA  DSKNO          ;the drive SELDSK chose
        ORI  20H            ;READ
        JMP  DOIO
WRITE   LDA  DSKNO
        ORI  40H            ;WRITE
DOIO    OUT  FDC            ;port 04h: the transfer happens here
        IN   FDC            ;its status
        ORA  A
        RZ                  ;0: OK
        MVI  A,1            ;CP/M's error
        RET
```

- **SELDSK** asks port 08h for the drive's format, returns 0 (no such
  disk) for format 00h, and otherwise fills the drive's disk parameter
  header with that format's DPB and, for a floppy, the skew table.

- **Media change**: the second `IN 08h` gives a count of the images mounted
  on the drive since power-on. CP/M 3's BIOS compares format and count with
  those at log-in and returns `0FFH` (media changed) from READ/WRITE when
  they differ, so the BDOS logs the drive in again. On hard disks it sets
  CKS to 0, not 8000H: the BDOS ignores media changes on permanent drives.

## Memory

See the [memory maps](memory-map.md) for both systems' layout and page
zero.

- CP/M 2.2 runs in bank 0; the BIOS's code and data must end below the
  boot ROM at FF00h, which ignores writes.
- A banked system selects banks with the [MMU](ports.md#mmu-40h); 0000h-BFFFh
  is banked, C000h-FFFFh common. CP/M 3 here keeps the BIOS's DPBs in
  common memory and the DPHs banked.

## Console and devices

- Console on ports 00h/01h, the second serial port on 02h/03h: CP/M 2.2's
  READER and PUNCH, CP/M 3's `TTY2` (AUX: from the cold boot; CON: and AUX:
  go to every device their vector selects, so `DEVICE` can put the console
  on both). Output waits while the port's TXBUSY bit is set.
- The clock on ports 41h/42h (CP/M 3's TIME function reads and, here, writes
  it).
- `BYE` and `RESET` style commands through [hardware control](ports.md#hardware-control-a0h).

## Building CPM3.SYS

The firmware's build assembles the CP/M 3 BIOS with DRI's RMAC and LINK and
runs GENCPM, under CP/M 3 in a host-side copy of the machine; the
`CPM3GEN` disk carries the result and the sources, so the same steps run on
the board ([CP/M 3](../cpm/cpm3.md#rebuilding-cpm3sys)).
