# Other systems

The machine has picosim's hardware, so the disk images for z80pack's
picosim run as they are, with their own BIOSes. Those BIOSes know only
four 8" floppy drives, A-D, and never ask the machine what a drive holds,
so the disk controller refuses them 4 MB and 8 MB images: a disk error,
instead of reading a hard disk as a floppy and writing over it.

## MP/M II

Copy `mpm-1.dsk` and `mpm-2.dsk` from z80pack's
[`picosim/disks`](https://github.com/udo-munk/z80pack/tree/master/picosim/disks)
into `/DISKS80`, mount `mpm-1` (boots) and `mpm-2`.

Its `MPM.SYS` is generated for picosim's six user banks. Most boards here
have two, and the machine stops (`MMU: no bank 3`) when MP/M selects a bank
it does not have: regenerate `MPM.SYS` for two banks with `GENSYS` from
`mpm-2` first. The PSRAM boards have 15 banks, enough for picosim's
`MPM.SYS` as it is.

MP/M II has not been tested on this firmware yet.

## Others

Anything that runs on picosim (its CP/M 1.4, CP/M 2.2 with its own BIOS,
...) should run here; systems with a BIOS of their own see drives A-D as
8" floppies only.
