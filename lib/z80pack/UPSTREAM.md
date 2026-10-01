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
| `picosim/disks/cpm22.dsk`, `picosim/disks/empty.dsk`, `cpmsim/disks/library/z80tests.dsk` | The built-in disk images (`cmake/diskimg.cmake`) |

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
