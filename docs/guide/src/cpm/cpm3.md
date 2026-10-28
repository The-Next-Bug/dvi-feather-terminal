# CP/M 3

Banked CP/M 3 (CP/M Plus) is built in: mount `CPM31` on A: and `CPM32` on
B: in the monitor (or pick the `CPM3` [drive set](drives.md#drive-sets))
and boot. Mount `CPM22` on A: again to go back.

## The system

- The banked system uses bank 1, so every build runs it; it has a 61K TPA.
- Its BIOS is this project's (`src/machine/cpm/cpm3/bnkbios3.asm`, made
  into `CPM3.SYS` when the firmware is built): eight drives, A-H, each any
  of the image formats, as under CP/M 2.2.
- Mounting another image on a drive while CP/M 3 runs needs no `^C`: the
  BIOS tells the BDOS the media changed.
- `CPM32` is an 8 MB disk with picosim's second floppy's files and room to
  spare.

## Search path and defaults

The BIOS sets `SETDEF`'s defaults at every cold boot:

- programs are found on the default drive, then A:, then B:, so `XLS`,
  `XPIP`, `XERA`, `MEM`, `HWINFO`, `AYTEST`, `AYPLAY`, `AYTRACK` and
  `WORMS` on `CPM32` run from any drive;
- COM files before SUB files;
- temporary files on B:, not the nearly full A:.

`README.TXT` on `CPM31` describes them and how to change them with a
`SETDEF` line in `PROFILE.SUB` (which otherwise only loads `VT100DYN`).

CP/M 3 shows the user number in its prompt (`3A>`) and finds system files
in user 0 from any user area.

## Date and time

`DATE` shows the date and time, and `DATE SET` sets them, together with the
board's real-time clock chip where it has one, so the time survives
power-off. See [Clock](../terminal/clock.md).

## Rebuilding CPM3.SYS

`CPM3GEN` holds the BIOS sources and `GENCPM.DAT`, `GENCPM`, the RELs, SPRs
and the `CPM3.SYS` the firmware's build made from them; `SYSGEN.SUB` lists
the steps (`RMAC` and `LINK` are on `CPM32`). To change the BIOS, edit its
source, assemble and link it, run `GENCPM`, and copy the new `CPM3.SYS` to
`CPM31`. See [Writing a BIOS](../reference/bios.md).
