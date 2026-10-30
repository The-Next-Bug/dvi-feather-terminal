# The monitor

At power-on in CP/M mode the monitor mounts the SD card and boots from
drive A after 3 seconds; any key stops that and shows its menu. It also
comes back when the machine stops (`BYE`, an error) and from SET-UP's
**M**. Its screen shows the card, what the USB host sees, and the drives.

| Key | Action |
|---|---|
| **Return** | boot from drive A (picosim's boot ROM loads track 0 sector 1) |
| **M** | mount a disk image on a drive A-H: the drive, then the image's name without `.DSK`; Return alone empties the drive. The assignments are kept in `/DISKS80/DRIVES.CFG` |
| **L** | list the images on the card (and the RAM disk), with their formats |
| **N** | make a new blank disk: a name, then 8" floppy, 4 MB or 8 MB (an 8 MB one takes a few seconds to write) |
| **I** | install the built-in disk images onto the card (below) |
| **F** | format the SD card (below) |
| **S** | CPU speed test: a JP loop for a second, in Z80 MHz |
| **C** | check the card again (after changing it) |
| **R** | resume the CPU, after leaving it from SET-UP |
| **F12** | SET-UP, also for the [firmware update](../terminal/firmware-update.md) (**U**) |

## The built-in disks (I)

The firmware carries these disk images, compressed in flash, to start a
blank card with; files already on the card are replaced only if you say so.

| Image | Format | Contents |
|---|---|---|
| `CPM22` | 8" floppy | CP/M 2.2 (boots) with its tools, [MEM](tools/mem.md), [HWINFO](tools/hwinfo.md), [WORMS](tools/worms.md), [XLS, XPIP, XERA](moving-files.md), [AYTEST](tools/aytest.md), [AYPLAY](tools/ayplay.md) and its tunes, [AYTRACK](tools/aytrack.md), `BYE`, `RESET` |
| `Z80TESTS` | 8" floppy | [TERMTEST](tools/termtest.md), [ALTSCR](tools/altscr.md), and the Z80 instruction exercisers `EXZ80DOC` and `EXZ80ALL` (about 10 minutes each) and `CPUTEST`. `PRELIM` needs an emulator that runs block moves in one go and wrecks CP/M here: reset after it |
| `EMPTY` | 8" floppy | a blank disk |
| `CPM31` | 8" floppy | [CP/M 3](cpm3.md) (boots) |
| `CPM32` | 8 MB | CP/M 3's tools, with this project's tools added |
| `CPM3GEN` | 8" floppy | CP/M 3's BIOS sources and what builds `CPM3.SYS` |
| `MPM1` | 8" floppy | [MP/M II](mpm.md) (boots) |
| `MPM2` | 8 MB | MP/M II's tools, XIOS source and what builds `MPM.SYS`, with this project's tools added |
| `F83` | 8 MB | [F83 Forth](f83.md) |

It also writes two [drive sets](drives.md#drive-sets) when they are
missing: `CPM22` (CPM22, Z80TESTS), `CPM3` (CPM31, CPM32) and `MPM`
(MPM1, MPM2).

## Formatting the card (F)

For a new card, or one the monitor cannot read ("no FAT file system").
Everything on the card is lost, so it asks you to type `FORMAT` and Return
first (Esc or anything else leaves the card alone). The format follows the
card's size: up to 2 GB FAT16 (FAT12 on the smallest), FAT32 above it (4 KB
clusters below 4 GB, 32 KB up to 64 GB, 64 KB beyond), in one partition
with the clusters on 4 MB boundaries, as the SD Association lays cards out.
Cards over 32 GB get FAT32 rather than exFAT, which the firmware does not
read; PCs read FAT32 cards of any size. A large card takes some seconds.
Then **I** puts the built-in disks on it.
