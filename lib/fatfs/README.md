# FatFs R0.15a

ChaN's FatFs (http://elm-chan.org/fsw/ff/), from `ff15a.zip`
(sha256 `74737b1cafa1a67a3f722dd0d1a44767c5b54d37b6300ad3825a904cbe88fc3c`),
1-clause BSD licence (`LICENSE.txt`). Only the files the terminal uses:
`ff.c`, `ff.h`, `diskio.h`, `ffconf.h`.

`ffconf.h` changes from the release: `FF_USE_FIND 1` (listing disk images),
`FF_USE_MKFS 1` (host tests; formatting a card), `FF_CODE_PAGE 437`,
`FF_FS_NORTC 1` with 2026-01-01 as the fixed file date, `FF_FS_TINY 1`
(open files share the volume's sector buffer instead of 512 bytes each:
SRAM over disk speed). No long file names: disk images are 8.3 names.

The disk I/O layer (`disk_*`) is `src/sdcard.c` on the board and
`test/diskio_ram.c` in the host tests.
