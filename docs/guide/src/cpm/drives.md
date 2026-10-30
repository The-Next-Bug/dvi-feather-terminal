# Drives and disk images

The machine has eight drives, A-H. Its disks are image files in `/DISKS80`
on the SD card, so a PC can copy them on and off, in three formats told
apart by their size (all 128-byte sectors, stored track by track):

| Format | Size | Geometry | CP/M capacity | cpmtools |
|---|---|---|---|---|
| 8" floppy (z80pack picosim) | 256256 bytes | 77 tracks x 26 sectors | 241 KB, 64 files | `ibm-3740` |
| 4 MB hard disk (z80pack cpmsim) | 4177920 bytes | 255 x 128 | 4080 KB, 1024 files | `z80pack-hd` |
| 8 MB hard disk | 8355840 bytes | 256 x 255 | 8160 KB, 1024 files | `dvi-hd8` |

cpmtools knows the first two; the 8 MB one is in `docs/cpmtools-diskdefs`
in the source: add it to cpmtools' `diskdefs` file, then for example
`cpmls -f dvi-hd8 DISK.DSK`. See [File formats](../reference/file-formats.md).

Drive A must hold a bootable 8" floppy: CP/M reloads itself from its system
tracks on every warm boot.

## Mounting

- In the [monitor](monitor.md): **M**, the drive, the image's name.
- While CP/M runs: the Drives panel (below).

The assignments are kept in `/DISKS80/DRIVES.CFG`. CP/M 2.2 sees a changed
drive after a warm boot, so type **^C** at the prompt afterwards (as after
swapping floppies); CP/M 3 sees it at once; under MP/M II type `DSKRESET`.
Don't change a drive a program is using.

## The Drives panel

F12 while CP/M runs opens SET-UP; **D** opens the Drives panel: what is
mounted on A-H, and a way to change it.

| Key | Action |
|---|---|
| Up, Down | select a drive (or the Set row) |
| Left, Right, Space | pick a disk image from the card for it |
| Return | mount the choices and save them |
| **S** | save the drives as a named set (type a name of up to 8 letters and digits; an existing name is replaced) |
| **R**, Delete | remove the set shown (after **Y**) |
| Esc | back to SET-UP |

## Drive sets

The Set row above A: steps through named sets of disks, each filling in
A-H; Return mounts them. It shows the set the drives match. To switch
systems, pick a set, Return, then **F11** (reset).

Sets are files in `/DISKS80/SETS`, `NAME.SET`, one line a drive as in
`DRIVES.CFG`, so they can be written on a PC too:

```
A=CPM31
B=CPM32
E=HD8
```

Drives left out are empty; a disk not on the card is left out (the panel
says how many). The monitor's install (**I**) writes two to start with,
`CPM22` and `CPM3`, if they are missing; after that they are yours to
change or remove. At most 16 sets.

## The RAM disk

On boards with PSRAM (`frankless`, `feather-rp2350-hstx-psram` and its
Adalogger variant) the PSRAM left over after the Z80's banks is a drive
image named `RAMDISK`, in the panel's list and the monitor's **M** and
**L** (also without a card), beside the card's images. It is the largest
format that fits: a 4 MB hard disk with an 8 MB PSRAM (an 8 MB one with
16 MB); the start-up lines say which.

It is fast and wears nothing, but it starts blank at power-on and every
restart (a Mode change, SET-UP's R, a firmware update) and is lost at
power-off: copy what you want to keep to a card image (`PIP`). It keeps its
files while the board runs, also when unmounted or after **F11**.
`DRIVES.CFG` and the sets can name it like any image (a `RAMDISK.DSK` on
the card is never used, and **N** does not make one).

## New disks

The monitor's **N** makes a blank image of any of the three formats.
