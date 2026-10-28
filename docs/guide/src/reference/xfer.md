# File transfer device (port 10)

Files in the folder `/XFER` of a FAT volume (the SD card or a USB stick),
read and written a 128-byte record at a time by DMA, in the style of the
disk controller. `XLS`, `XPIP` and `XERA` use it (`src/machine/cpm/xfer.inc`
has their helpers); the device is `src/disks/xfer.c`.

## Commands

`OUT 10` a command: the high nibble is the command, the low nibble the
volume (0 the SD card, 1 the USB stick that is `USB:`). `IN 10` reads the
status of the last command.

| Command | Name | Input (at the DMA address) | Output (at the DMA address) |
|---|---|---|---|
| `00h` | RESET | | closes the open file and the listing |
| `10h` | SETDMA | the next two `OUT 10`s: the DMA address, low then high | |
| `20h` | FIND FIRST | a pattern (11 bytes) | the first entry (15 bytes) |
| `30h` | FIND NEXT | | the next entry |
| `40h` | OPEN | a name | (the file opened to read) |
| `50h` | CREATE | a name | (a new file to write; not over an existing one) |
| `60h` | READ | | the next record; the last padded with `^Z` |
| `70h` | WRITE | a record | (appended to the file) |
| `80h` | CLOSE | | |
| `90h` | CLOSE TEXT | | closes, cut at the first `^Z` of the last record |
| `A0h` | DELETE | a name | |

Banking applies to the DMA address, as for the disk controller.

## Data

- **Names** are 11 bytes in CP/M's FCB form: `NAME    EXT`, blank-padded,
  upper case; in a pattern `?` matches any character.
- **Listing entries** are 15 bytes: the name, then the size in bytes,
  32-bit little-endian. Names CP/M cannot take are left out; long names
  appear as their 8.3 short forms.
- **Records** are 128 bytes.

## Status

| Value | Meaning |
|---|---|
| 0 | OK |
| 1 | not found: no such file, or the end of the listing |
| 2 | exists: CREATE found the file there |
| 3 | end of file: READ has no more records |
| 4 | I/O error, or the volume is full |
| 5 | no volume: no card or stick, or no FAT file system on it |
| 6 | bad: a bad name, an unknown command, or no file open |

One file and one listing are open at a time. RESET runs at a machine reset,
when the monitor checks the card again, and before a firmware update.

## Example: reading a file

```
        LD   A,10h        ; SETDMA
        OUT  (10),A
        LD   HL,BUF
        LD   A,L
        OUT  (10),A
        LD   A,H
        OUT  (10),A
        ; BUF holds "README  TXT"
        LD   A,40h        ; OPEN, volume 0 (the SD card)
        OUT  (10),A
        IN   A,(10)
        OR   A
        JR   NZ,ERROR
NEXT:   LD   A,60h        ; READ: the next record into BUF
        OUT  (10),A
        IN   A,(10)
        CP   3            ; end of file?
        JR   Z,DONE
        ...               ; use the record
        JR   NEXT
DONE:   LD   A,80h        ; CLOSE
        OUT  (10),A
```
