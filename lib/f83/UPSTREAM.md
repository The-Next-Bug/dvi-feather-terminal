# F83 (vendored)

Henry Laxen and Michael Perry's F83, FORTH-83 version 2.0 for CP/M-80:
the files of F83V2-80.ARK, the built-in disk F83 (`src/disks/diskimg.cmake`,
bd xdu). F83.COM runs as it is (8080 code, BDOS console and files); the
disk's copy has src/machine/cpm/f83/viewdrv.fth loaded into it (VIEW
reads the source from F83's own drive, CP/M 3's 0050H), made at build
time by src/machine/cpm/f83/build.sh. These files stay unchanged.

- Source: F83V2-80.ARK (Keith Petersen, W8SDZ, 5 Feb 1987), from
  http://www.retroarchive.org/cpm/cdrom/CPM/FORTH-83/F83V2-80.ARK
  (also http://www.forth.org/library/eforth_SOC/eforth_SOC_source/f83/f83v2-80.ark),
  MD5 `8c5017e406add5aa0dcd9e99a2514bc2`
- Vendored: 2026-10-18, extracted from the ARK (all 13 files crunched,
  method 8; every file's length and CRC-16 as the ARK lists them)
- Licence: public domain. From README.80: "This is a public domain
  system, and may be freely distributed and copied, as long as the
  authors are given credit and no copyright notice is placed upon it."

github.com/janaite/forth83-80 mirrors the same ARK, but its readme.80 is
cut short (17985 of 18048 bytes); the other files are the same.

The files are kept byte for byte (CP/M text: CR LF, ^Z padding; .BLK
files are 1 KB blocks of 64-column lines), so `.gitattributes` marks them
binary.

| File | What |
|------|------|
| `F83.COM` | The system, ready to run |
| `-READ.ME` | The ARK's notes (README.1ST in the LBR) |
| `README.80` | Laxen and Perry's instructions |
| `F83-FIXS.TXT` | Changes from F83 1.0 |
| `KERNEL80.BLK`, `META80.BLK`, `CPU8080.BLK`, `EXTEND80.BLK`, `UTILITY.BLK` | The system's source (metacompiler, kernel, extensions) |
| `HUFFMAN.BLK`, `CLOCK.BLK`, `EXPAND80.BLK`, `BASIC.BLK` | Applications: compression, a calendar, the .HUF expander, a BASIC compiler |
