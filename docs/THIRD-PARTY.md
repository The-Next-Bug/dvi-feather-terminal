# Third-party components

The firmware image (`dvi_terminal-*.uf2` / `.bin`) contains code and data
from the projects below. Their licence texts are in `licenses/` in the
release package (`scripts/package.sh` copies them from the sources the build
used).

| Component | Used for | Licence | Text |
|---|---|---|---|
| [Raspberry Pi pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.3.1 | Runtime, hardware drivers; `src/hstx_dvi.c` follows its pico-examples HSTX DVI example (same licence) | BSD-3-Clause | `licenses/pico-sdk-BSD-3-Clause.txt` |
| CMSIS headers (in pico-sdk) | Cortex-M33 core definitions | Apache-2.0 | `licenses/Apache-2.0.txt` |
| [TinyUSB](https://github.com/hathach/tinyusb) (in pico-sdk) | USB keyboard (host) | MIT | `licenses/tinyusb-MIT.txt` |
| [z80pack](https://github.com/udo-munk/z80pack) | Z80 CPU core, disk controller (`lib/z80pack/z80core`, `iodevices/sd-fdc.c`); `src/machine.c` and `src/z80pack/` follow its picosim | MIT | `licenses/z80pack-MIT.txt` |
| z80pack `z80core/log.h` (David McNaughton, from ESP-IDF) | Logging macros compiled into the CPU core | Apache-2.0 | `licenses/Apache-2.0.txt` |
| [FatFs](http://elm-chan.org/fsw/ff/) R0.15a (ChaN) | FAT file system on the SD card (`lib/fatfs`) | 1-clause BSD | `licenses/FatFs-BSD-1-Clause.txt` |
| [Terminus Font](https://terminus-font.sourceforge.net/) 8x16 (Dimitar Toshkov Zhekov) | 80-column glyphs (the generated table is a derivative) | SIL OFL 1.1 | `licenses/Terminus-OFL-1.1.txt` |
| misc-fixed 6x13 (X.Org) | 132-column glyphs | Public domain | - |
| newlib and libgcc (Arm GNU Toolchain) | C library and compiler runtime | newlib: BSD-style licences; libgcc: GPL-3.0 with the GCC Runtime Library Exception | in the toolchain's `license.txt` |

## Built-in CP/M disk images

The firmware carries three disk images from z80pack, compressed, which the
Z80 machine's monitor can write to an SD card:

- `CPM22` (z80pack `picosim/disks/cpm22.dsk`): Digital Research CP/M 2.2
  and its utilities, with z80pack's tools; its system tracks hold CP/M's
  CCP and BDOS moved to 62K and our BIOS and boot loader (`src/cpm`,
  derived from z80pack picosim's, MIT). CP/M is © Digital
  Research / Caldera. In 2001 Lineo (Caldera's successor) granted the right
  to use, distribute and modify the CP/M material made available by "The
  Unofficial CP/M Web Site" (http://www.cpm.z80.de/license.html); z80pack
  and this firmware redistribute CP/M on that basis. It is not an
  open-source licence: the grant was made to that site and its community.
- `Z80TESTS` (z80pack `cpmsim/disks/library/z80tests.dsk`): Z80 instruction
  exercisers `EXZ80DOC` and `PRELIM` (© 1994 Frank D. Cringle, GPL-2.0 or
  later, as modified for z80pack; their sources `EX.MAC`, `EXZ80DOC.MAC` and
  `PRELIM.MAC` are on the same disk; licence text
  `licenses/GPL-2.0.txt`), and `CPUTEST` (SuperSoft Diagnostics II), as
  distributed with z80pack.
- `EMPTY`: a blank, formatted CP/M disk.
