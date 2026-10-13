# CP/M 2.2 system for the Z80 machine

The built-in `CPM22` disk is z80pack's picosim `cpm22.dsk` with its system
tracks replaced by a 62K CP/M 2.2 with eight drives (A-H), each of which can
hold an 8" floppy, a 4 MB or an 8 MB hard disk image (`src/drives.h`).
`cmake/diskimg.cmake` builds it on every build:

1. `build.sh` compiles z80pack's `z80asm` (`lib/z80pack/z80asm`) with the
   host C compiler and assembles `boot.asm` (the boot loader, track 0
   sector 1) and `bios.asm` (the BIOS), checking that the BIOS's code and
   tables fit the 7 sectors left on the two system tracks and that its data
   area (8 x 255-byte allocation vectors, 8 x 16-byte check vectors) ends
   below the boot ROM page at FF00H.
2. `src/diskimg_gen.py --system` takes the CCP and BDOS from z80pack's 64K
   `picosim/srccpm2/cpm.bin`, moves them down to 62K with the relocation
   bitmap `ccpbdos.rel`, and writes loader, CCP, BDOS and BIOS to the system
   tracks before compressing the image.
3. `build.sh` also assembles `worms.asm`, `xls.asm`, `xpip.asm` and
   `xera.asm` (Z80 mnemonics; the last three include `xfer.inc`, the port 10 constants and a
   command-tail parser for `SD:`/`USB:` specs, so z80asm runs from this
   folder) into `.com` files, and `diskimg_gen.py --file CPM22 WORMS.COM`
   (and XLS, XPIP, XERA) writes them into the image's CP/M directory (user 0; the
   build fails if the name is taken or the disk is full). Programs of our
   own go on the disk the same way. `--delete CPM22 XDIR.COM` takes
   z80pack's XDIR off first (it only works on drives A and B).
   `termtest.asm` (the terminal types, page by page) and `altscr.asm`
   (the alternate screen) go onto the `Z80TESTS` disk the same way
   (`--file Z80TESTS TERMTEST.COM`, `ALTSCR.COM`).

`ccpbdos.rel` has one bit per byte of the 5632-byte CCP+BDOS, set where the
byte is the high byte of an address inside them. It was made once by running
`MOVCPM 62 *` (from the same disk) in the emulator and comparing its CCP+BDOS
(memory 0980H, as `SAVE 34 CPM62.COM` would store them) with the 64K ones:
the 1072 bytes that differ all differ by exactly 8 pages, the 2 KB the system
moved. Any other size could be made the same way; `MSIZE` in `boot.asm` and
`bios.asm` and the 62 in `cmake/diskimg.cmake` must agree.

The BIOS asks the machine's port 8 for the format of the image on a drive
when CP/M selects it (`machine.h`) and fills in that drive's disk parameter
header: the skew table and the floppy parameters, or no skew and one of the
hard disk ones. CP/M only selects a drive again after a warm boot, so after
changing a disk type `^C` at the prompt.

| Format | Tracks x sectors | Block | Directory | Capacity | Reserved tracks |
|---|---|---|---|---|---|
| 8" floppy | 77 x 26 (skew 6) | 1 KB | 64 | 241 KB | 2 (the system) |
| 4 MB hard disk (z80pack's) | 255 x 128 | 2 KB | 1024 | 4080 KB | 0 |
| 8 MB hard disk | 256 x 255 | 4 KB | 1024 | 8160 KB | 0 |

cpmtools definitions for them are in `docs/cpmtools-diskdefs`.

The MP/M II images z80pack ships keep their own BIOS: four floppy drives,
A-D, which never asks port 8, so the machine refuses it hard-disk images
(`machine.h`). On `CPM22` itself, `BIOS.ASM`, `MOVCPM.COM` and
`SYSGEN64.SUB` are still picosim's 64K four-drive system: running them would
replace this one.

## CP/M 3 (`cpm3/`)

picosim's banked CP/M 3 disks are built in as `CPM31` (boot disk), `CPM32`
(with WORMS, XLS, XPIP, XERA and MEM added by `--file CPM32`) and
`CPM3GEN` (bd f8w). `CPM31`'s `CPM3.SYS` is ours, made at build time:

- `cpm3/bnkbios3.asm`: picosim's banked BIOS (8080, RMAC) with eight drives,
  A-H, of any of the formats above. SELDSK asks port 8 when the BDOS logs a
  drive in and puts that format's DPB (common memory: BDOS function 31
  hands programs its address) and skew in the drive's DPH (banked). GENCPM
  sizes the vectors from `DPBMAX`, the DPB every DPH starts with: the most
  blocks and checksums of any format. READ and WRITE ask port 8 again and
  return 0FFH, media changed, when the drive's format or mount count
  differs from its log-in's, so the BDOS logs it in again. Hard disks have
  no checksums but are not marked permanent (8000H), whose media changes
  the BDOS ignores. No hashing, as in picosim's.
- `cpm3/gencpm.dat`: picosim's, with 16 directory buffers for E-H too and
  bank 0's segment cut to 0000H-76FFH (the BIOS's vectors and buffers grew
  into the rest). The TPA is 61K, a page more than picosim's.
- `cpm3/build.sh` (from `cmake/diskimg.cmake`): builds `cpm3/cpmrun.c`, a
  host program that runs `src/machine.c` on plain 8" images with a script
  at the console, and runs `cpm3/sysgen.run`: picosim's `SYSGEN.SUB` steps
  (`RMAC`, `LINK`, `GENCPM AUTO`) with `CPM31` on A:, a copy of `CPM3GEN`
  holding our sources on B: and `CPM32` (RMAC, LINK) on C:. RMAC's error
  lines, LINK's undefined symbols, any GENCPM error or a TPA under 61K fail
  the build. The work disk becomes the built-in `CPM3GEN`, so its RELs,
  SPRs and `CPM3.SYS` are the shipped ones. Run unchanged on picosim's
  BIOS, this makes picosim's `CPM3.SYS` byte for byte.

`diskimg_gen.py --edit IMAGE` changes a raw image in place (`--delete`,
`--file`, `--extract`), for the work disks.
