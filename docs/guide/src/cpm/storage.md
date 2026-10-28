# SD card and USB sticks

## The SD card

An SPI microSD adapter for 3.3 V, wired as the board's header says (see
[Boards](../start/boards.md)). The card must have a FAT file system
(FAT12, FAT16 or FAT32). Cards up to 32 GB come as FAT32; bigger ones come
as exFAT, which the firmware does not read: format them as FAT32 on a PC,
or with the monitor's [**F**](monitor.md#formatting-the-card-f).

What the firmware keeps on the card:

| Path | What |
|---|---|
| `/DISKS80/*.DSK` | the disk images (see [Drives and disk images](drives.md)) |
| `/DISKS80/DRIVES.CFG` | which image is on which drive |
| `/DISKS80/SETS/*.SET` | named [drive sets](drives.md#drive-sets) |
| `/XFER/` | files to move to and from CP/M (see [Moving files](moving-files.md)) |
| `/FIRMWARE.UF2` | a [firmware update](../terminal/firmware-update.md) waiting to be installed (`FIRMWARE.OLD` once installed) |
| `/USBLOG.TXT` | the [USB log](../terminal/troubleshooting.md#the-usb-log), when it is on |

## USB sticks

USB sticks go into the USB port through the hub, next to the keyboard.
They are `USB:` to the file transfer tools and can hold a firmware update.

- The stick must be FAT12, FAT16 or FAT32 with 512-byte sectors: most
  sticks up to 32 GB come that way; larger ones are usually exFAT and need
  formatting as FAT32 first. A card reader shows its first slot only.
- **One stick at a time is `USB:`**: the first one plugged in. Another
  plugged in meanwhile waits (`USB+1` on the status line) and takes over
  when the first is pulled. A stick that cannot be used (not FAT, or exFAT)
  is passed over (`USB?`) until it is plugged in again.
- **Pull a stick between copies**, when `USB` on the status line is not
  lit: every file is closed when `XPIP` finishes with it. Pulled in the
  middle of a copy, the copy stops with an error and the file on the stick
  may be incomplete; the copy never carries on onto another stick.
