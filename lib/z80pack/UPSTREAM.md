# z80pack (vendored subset)

The parts of z80pack this project builds, copied from upstream instead of
a git submodule so the Z80 core can carry local fixes.

- Upstream: https://github.com/udo-munk/z80pack
- Commit: `91fd28eb04e675c2127df88ed3f40675e15282e2` (2025-06-17, "Merge pull
  request #549 from udo-munk/dev")
- Vendored: 2026-10-01
- Licence: MIT, `LICENSE` (whole project), `z80core/COPYING`,
  `z80asm/COPYING`. Copyright Udo Munk, Thomas Eberhardt and others.

The directory layout is upstream's, so paths match the upstream tree.

## Files

| Path | Used for |
|------|----------|
| `z80core/simglb.[ch]`, `simcore.[ch]`, `simz80.[ch]`, `simz80-{cb,dd,ddcb,ed,fd,fdcb}.[ch]`, `simdefs.h`, `log.h` | The Z80 core (`cmake/z80pack.cmake`), configured by `src/z80pack/*.h` in place of a machine's `sim.h` |
| `iodevices/sd-fdc.[ch]` | picosim's disk controller, port 4 |
| `z80asm/` | The assembler, built for the host to assemble `src/cpm` (`src/cpm/build.sh`) |
| `picosim/srccpm2/cpm.bin` | picosim's 64K CCP+BDOS, relocated to 62K (`src/cpm`) |
| `picosim/disks/cpm22.dsk`, `picosim/disks/empty.dsk`, `cpmsim/disks/library/z80tests.dsk` | The built-in disk images (`cmake/diskimg.cmake`); z80tests.dsk changed, see below |
| `picosim/disks/cpm3-1.dsk`, `cpm3-2.dsk`, `cpm3gen.dsk` | Banked CP/M 3 (picosim's BNKBIOS3, DRI's BDOS and utilities): the built-in CPM31, CPM32 and CPM3GEN disks (bd f8w.1) |

Everything else upstream (the other machines, the 8080 core, the
alternative `ALT_Z80` core, the front panel, other disks) is left out.

## Local changes

Each change is marked in the source with a `/* local: ... */` comment and
listed here with its bd issue. To take a newer upstream, diff upstream at the
commit above against its new version and port the changes here.

- `z80core/simz80-dd.c`, `z80core/simz80-fd.c`, `trap_dd`/`trap_fd`: a
  DD or FD prefix before an opcode that does not use HL is ignored, as on a
  real Z80, instead of stopping the CPU with OPTRAP2 (bd lpt.10; EXZ80DOC's
  `ld <bcdexya>,<bcdexya>` test stopped at DD 40).
- `z80core/simdefs.h` and all of `z80core/simz80*.c`: the undocumented
  flags Y and X (F bits 5 and 3) are set as a Z80 sets them, through the
  `SET_XY`/`SET_XY_BLOCK` macros (simdefs.h says where each comes from);
  upstream never set them (bd lpt.6; CPUTEST failed test 0025h). Not
  modelled: MEMPTR, so `BIT n,(HL)` leaves them as they were.
- `cpmsim/disks/library/z80tests.dsk`: added `EXZ80ALL.COM` (EX.MAC built
  with `exkind equ 2`, all flags tested; z80asm reproduces the shipped
  EXZ80DOC.COM exactly from EXZ80DOC.MAC, bar `ds` fill bytes) and its
  source wrapper `EXZ80ALL.MAC`.
