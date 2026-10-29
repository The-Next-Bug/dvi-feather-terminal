# Memory maps

**How the maps are drawn.** Every map in this guide puts the **highest
address at the top** and the lowest at the bottom, as CP/M's manuals do:
"top of memory" is the highest address, "above" means at higher
addresses, "below" at lower ones. Ranges are written low-high and include
both ends (`C000h-FFFFh` is 16 KB). Addresses are hex, marked with `h`, as
on the [Z80 I/O ports](ports.md) page.

## The Z80's address space

64 KB, of which the top 16 KB is always the same memory and the lower
48 KB is whichever bank the [MMU](ports.md#mmu-40h) (port 40h) selects:

```
              bank 0         bank 1        bank 2   ...   bank n (n <= 15)
 FFFFh  +--------------------------------------------------------------+
        |  boot ROM (read only)                                        |
 FF00h  +--------------------------------------------------------------+
 FEFFh  |                                                              |
        |  common memory: the same 16 KB whatever bank is selected     |
 C000h  +--------------+--------------+--------------+     +-----------+
 BFFFh  |              |              |              |     |           |
        |  bank 0's    |  bank 1      |  bank 2      | ... |  bank n   |
        |  lower 48 KB |  48 KB       |  48 KB       |     |  48 KB    |
 0000h  +--------------+--------------+--------------+     +-----------+
```

| Range | Size | What |
|---|---|---|
| FF00h-FFFFh | 256 bytes | the boot ROM: picosim's 44-byte loader, then FFh; reads only, writes are ignored |
| C000h-FEFFh | 16,128 bytes | common RAM (bank 0's top) |
| 0000h-BFFFh | 48 KB | RAM of the selected bank: bank 0's lower 48 KB, or bank 1-15 |

Bank 0 is 64 KB: its lower 48 KB plus the common 16 KB. The other banks
are 48 KB each: 2 on most boards (in the RP2350's SRAM), 15 on the PSRAM
boards (in the PSRAM; see [below](#psram)). `IN 40h` gives the count, and
[MEM](../cpm/tools/mem.md) shows it.

**At power-on** all RAM holds random bytes, as a real machine's would; the
CPU starts at FF00h in bank 0. **A reset** (F11, `RESET.COM`) keeps the
RAM, selects bank 0, stops the timer and starts the boot ROM again, which
reads track 0, sector 1 of drive A to 0000h-007Fh and jumps to 0000h (its
disk descriptor is at F000h-F003h; see
[the boot ROM](ports.md#example-the-boot-rom)).

## Page zero under CP/M

The bottom 256 bytes of the TPA's bank, as CP/M sets them up:

| Range | What |
|---|---|
| 0080h-00FFh | the default DMA buffer; at a program's start, the command tail (length, then the text) |
| 006Ch-007Fh | the second default FCB (its first 16 bytes) |
| 005Ch-006Bh | the first default FCB |
| 0050h-005Bh | CP/M 3: 0050h the drive the program was loaded from (1 = A), 0051h-005Bh the password pointers |
| 0040h-004Fh | BIOS scratch; CP/M 2.2's BIOS keeps its [disk descriptor](ports.md#the-disk-descriptor) at 0040h-0043h |
| 0038h-003Ah | `RST 38H`: the [60 Hz timer](ports.md#timer-43h)'s interrupt, and debuggers' breakpoints |
| 0008h-0037h | `RST 08H`-`RST 30H`, free |
| 0005h-0007h | `JP` the BDOS: 0006h-0007h is the address of the TPA's top + 1 |
| 0004h | the current drive (bits 0-3, 0 = A) and user (bits 4-7) |
| 0003h | the IOBYTE |
| 0000h-0002h | `JP` the BIOS's warm boot |

## CP/M 2.2 (62K)

Everything is in bank 0 (`src/machine/cpm/bios.asm`, `boot.asm`); banks 1
and up are free for programs (see [MEM T](../cpm/tools/mem.md)).

| Range | What |
|---|---|
| FF00h-FFFFh | boot ROM |
| FDA0h-FEFFh | unused |
| F426h-FD9Fh | the BIOS's data: disk parameter headers, directory buffer, allocation and check vectors (not loaded from disk) |
| F200h-F425h | BIOS: the jump table at F200h, the disk parameter blocks, the code |
| E400h-F1FFh | BDOS, entered at E406h |
| DC00h-E3FFh | CCP |
| 0100h-DBFFh | TPA |
| 0000h-00FFh | [page zero](#page-zero-under-cpm) |

A program may also use the CCP's space, as the CCP is reloaded at every
warm boot: MEM counts the TPA up to E405h, below the BDOS's entry
(`TPA 0100H-E405H`). At a cold boot
the boot sector (loaded to 0000h by the boot ROM) reads the CCP, BDOS and
BIOS from the system tracks of drive A to DC00h and jumps to the BIOS at
F200h. The build checks that the BIOS's data ends below FF00h.

## CP/M 3 (banked)

CP/M 3 (`src/machine/cpm/cpm3/bnkbios3.asm`, made by GENCPM) uses two
banks: programs run in bank 1, the system's banked half is in bank 0, and
the resident parts are in common memory.

```
             bank 0                  bank 1
 FFFFh  +---------------------------------------------+
        |  boot ROM                                   |
 FF00h  +---------------------------------------------+
        |  resident BIOS                              |
 FC00h  +---------------------------------------------+
        |  resident BDOS (entry F606h)                |
 F600h  +---------------------------------------------+
        |  TPA, top part (RSXs load here, at its top) |
 C000h  +----------------------+----------------------+
        |  banked BIOS         |                      |
 9E00h  +----------------------+                      |
        |  banked BDOS         |  TPA                 |
 7000h  +----------------------+                      |
        |  directory buffers,  |                      |
        |  hash tables         |                      |
 0000h  +----------------------+----------------------+
                                  (page zero at 0000h-00FFh)
```

| Range | Bank | What |
|---|---|---|
| FF00h-FFFFh | common | boot ROM |
| FC00h-FEFFh | common | the BIOS's resident part (jump table, bank switching, the DPBs) |
| F600h-FBFFh | common | resident BDOS, entered at F606h |
| 0100h-F5FFh | 1 | TPA; MEM counts it up to F605h, below the BDOS's entry, and less with RSXs loaded |
| 9E00h-BFFFh | 0 | banked BIOS |
| 7000h-9DFFh | 0 | banked BDOS |
| 0000h-6FFFh | 0 | directory buffers and hash tables (GENCPM's) |

Banks 2 and up are not used by CP/M 3. PROFILE.SUB loads VT100DYN, an RSX
at the TPA's top, so MEM shows `TPA 0100H-F205H` on the built-in disks.
These addresses are from GENCPM's run in the build
(`build/<target>/generated/cpm3/sysgen.log`) and move when the BIOS
changes; MEM shows the running system's TPA.

## The RP2350's memory

What the firmware keeps where, for hacking on it (see
[How it works](../hacking/how-it-works.md#memory)).

### Flash

Mapped at 10000000h; offsets below are from the start of the flash. The
size comes from the board: 4 MB on the Pico 2, 8 MB on the Feather, 16 MB
on the Frankless.

| Area | 4 MB | 8 MB | 16 MB |
|---|---|---|---|
| left blank (flashing a UF2 file erases it) | 3FF000h-3FFFFFh | 7FF000h-7FFFFFh | FFF000h-FFFFFFh |
| the settings, two sectors used in turn | 3FD000h-3FEFFFh | 7FD000h-7FEFFFh | FFD000h-FFEFFFh |
| the firmware update's staging area (used only during an update) | 200000h-3FCFFFh | 400000h-7FCFFFh | 800000h-FFCFFFh |
| free: the firmware's room to grow | the firmware's end-1FFFFFh | the firmware's end-3FFFFFh | the firmware's end-7FFFFFh |
| the firmware (about 1.2 MB: code, fonts, the logo, the built-in disk images) | from 000000h | from 000000h | from 000000h |

The staging area is the upper half of the flash less the top 12 KB, and
it caps the firmware's size: an image must fit in it to be updated.

### SRAM

520 KB at 20000000h-20081FFFh: 512 KB of main SRAM and two 4 KB scratch
banks.

| Range | What |
|---|---|
| 20081000h-20081FFFh | scratch Y: core 0's stack (2 KB) |
| 20080000h-20080FFFh | scratch X (core 1's stack is in .bss) |
| `__end__`-2007FFFFh | free (nothing uses a heap) |
| 20000000h-`__end__` | the code (it runs from SRAM), data and .bss: the Z80's memory (bank 0 and the SRAM banks), the raster, the fonts in use, the screen |

`__end__` is in the build's `.elf.map`. With two Z80 banks in SRAM about
12 KB stays free (the Pico 2; 9 KB on the Feather with an Adalogger); the
PSRAM boards, whose banks are in the PSRAM, keep about 100 KB free.

### PSRAM

On the PSRAM boards, 8 MB at 11000000h, given out from the bottom in CP/M
mode (`src/platform/extram.c`); terminal mode does not use it.

| Range | What |
|---|---|
| 114B0000h-117FFFFFh | free (3.3 MB) |
| 110B4000h-114AFFFFh | the RAM disk: a 4 MB disk image (the largest format that fits), blank at every power-on |
| 11000000h-110B3FFFh | Z80 banks 1-15, 48 KB (C000h bytes) each: bank n at 11000000h + (n − 1) × C000h |
