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

The CP/M 3 and MP/M II images z80pack ships keep their own BIOSes: four
floppy drives, A-D. On `CPM22` itself, `BIOS.ASM`, `MOVCPM.COM` and
`SYSGEN64.SUB` are still picosim's 64K four-drive system: running them would
replace this one.
