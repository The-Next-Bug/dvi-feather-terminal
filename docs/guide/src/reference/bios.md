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

- Use the [disk controller](ports.md#disk-controller-4) on port 4:
  128-byte sectors by DMA, the sector number 1-based.
- Ask [port 8](ports.md#drive-format-8) what each drive holds and pick
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

- **Media change**: the second `IN 8` gives a count of the images mounted
  on the drive since power-on. CP/M 3's BIOS compares format and count with
  those at log-in and returns `0FFH` (media changed) from READ/WRITE when
  they differ, so the BDOS logs the drive in again. On hard disks it sets
  CKS to 0, not 8000H: the BDOS ignores media changes on permanent drives.

## Memory

- CP/M 2.2 runs in bank 0.
- A banked system selects banks with the [MMU](ports.md#mmu-64); 0000H-BFFFH
  is banked, C000H-FFFFH common. CP/M 3 here keeps the BIOS's DPBs in
  common memory and the DPHs banked.

## Console and devices

- Console on ports 0/1, the second serial port on 2/3.
- The clock on ports 65/66 (CP/M 3's TIME function reads and, here, writes
  it).
- `BYE` and `RESET` style commands through [hardware control](ports.md#hardware-control-160).

## Building CPM3.SYS

The firmware's build assembles the CP/M 3 BIOS with DRI's RMAC and LINK and
runs GENCPM, under CP/M 3 in a host-side copy of the machine; the
`CPM3GEN` disk carries the result and the sources, so the same steps run on
the board ([CP/M 3](../cpm/cpm3.md#rebuilding-cpm3sys)).
