# Other systems

The machine has picosim's hardware, so the disk images for z80pack's
picosim run as they are, with their own BIOSes. Those BIOSes know only
four 8" floppy drives, A-D, and never ask the machine what a drive holds,
so the disk controller refuses them 4 MB and 8 MB images: a disk error,
instead of reading a hard disk as a floppy and writing over it.

## picosim's MP/M II

MP/M II is [built in](mpm.md). picosim's own `mpm-1.dsk` and `mpm-2.dsk`
(z80pack's
[`picosim/disks`](https://github.com/udo-munk/z80pack/tree/master/picosim/disks))
run too: `mpm-1` boots CP/M 2.2, from which `MPMLDR` starts MP/M. Their
`MPM.SYS` is generated for six user banks: on a board without PSRAM the
machine stops (`MMU: no bank 3`) when it selects a bank the board does not
have.

## Others

Anything that runs on picosim (its CP/M 1.4, CP/M 2.2 with its own BIOS,
...) should run here; systems with a BIOS of their own see drives A-D as
8" floppies only.
