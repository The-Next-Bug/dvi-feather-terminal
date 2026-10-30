# MP/M II

MP/M II, Digital Research's multi-user, multitasking CP/M, is built in:
mount `MPM1` on A: and `MPM2` on B: in the monitor (or pick the `MPM`
[drive set](drives.md#drive-sets)) and boot. MP/M starts straight away:
`MPM1` holds its loader on the system tracks. Its prompt has the user
number in front of the drive (`0A>`). Mount `CPM22` on A: again to go
back.

## The system

- Two consoles: the screen and keyboard are console 0, the serial port
  (the terminal UART, at SET-UP's speed) is console 1. Connect a terminal
  there and press Return for its own prompt. Both run programs at the same
  time. Console 1 starts in user 1 (`1A>`); `USER 0` gets it to the files
  in user 0, such as the tools on `MPM2`.
- Each running program gets a memory segment of its own. The firmware's
  `MPM.SYS` is made for the board's banks when the firmware is built:

  | Boards | User memory segments |
  |---|---|
  | With PSRAM (Frankless, the Feather with PSRAM) | seven, banks 1-7, 48 KB each (MP/M II's most) |
  | Without (the Sock, the Feather without PSRAM) | banks 1 and 2, 48 KB each, and bank 0 below the system, about 28 KB |

- Its XIOS is this project's (`src/machine/cpm/mpm/bnkxios.mac`, from
  picosim's): eight drives, A-H, each any of the image formats, as under
  CP/M 2.2 and CP/M 3. After mounting another image on a drive while
  MP/M runs, type `DSKRESET` (or `DSKRESET F:` for one drive), MP/M's
  counterpart of CP/M 2.2's `^C`: until then reading or writing that drive
  fails (`Bdos Err On F: Bad Sector`) rather than using the old disk's
  layout on the new one.
- `MPM2` is an 8 MB disk: picosim's MP/M work disk, with this project's
  tools added (`XLS`, `XPIP`, `XERA`, `MEM`, `HWINFO`, `AYTEST`, `AYPLAY`,
  `AYTRACK`, `WORMS`). MP/M finds programs on the current drive, then
  system files on A:, so run them from B: (`B:HWINFO`) or with `B:` as the
  current drive.
- Resident processes: the spooler (`SPOOL`), `ABORT`, `SCHED` (run a
  program at a time of day), `MPMSTAT` (the system's state) and HWCLOCK.

## Date and time

HWCLOCK sets MP/M's time of day from the board's clock at boot and every
minute after, so `TOD` shows the date and time without setting them. See
[Clock](../terminal/clock.md).

## Rebuilding MPM.SYS

`MPM2` holds the XIOS source, `BNKXIOS.SPR`, the other SPRs and RSPs,
`GENSYS` and the `SYSTEM.DAT` the firmware's build made `MPM.SYS` with;
`SYSGEN.SUB` lists the steps: `M80` (on `MPM2`) assembles the XIOS, `LINK`
(on `MPM1`) makes it into `BNKXIOS.SPR`, `GENSYS $$A` makes `MPM.SYS` with
`SYSTEM.DAT`'s answers, and the new `MPM.SYS` goes to `MPM1`. Run
`GENSYS` without `$$A` to change the answers (the number of memory
segments, the resident processes, ...).
