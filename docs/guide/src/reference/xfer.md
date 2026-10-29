# File transfer device (port 0Ah)

Files in the folder `/XFER` of a FAT volume (the SD card or a USB stick),
read and written a 128-byte record at a time by DMA, in the style of the
[disk controller](ports.md#disk-controller-04h). `XLS`, `XPIP` and `XERA`
use it (`src/machine/cpm/xfer.inc` has their helpers); the device is
`src/disks/xfer.c`. Ports and values are hex, as on the
[Z80 I/O ports](ports.md) page.

## Commands

**`OUT 0Ah`: a command**

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| | CMD | CMD | CMD | CMD | VOL | VOL | VOL | VOL |

- **CMD** (bits 4-7): the command, below.
- **VOL** (bits 0-3): the volume: 0h the SD card, 1h the USB stick that
  is `USB:`. FIND FIRST, OPEN, CREATE and DELETE use it; the others act on
  the open file or listing, whatever their volume bits.

| Command | Name | Input (at the DMA address) | Output (at the DMA address) |
|---|---|---|---|
| `00h` | RESET | | (closes the open file and the listing) |
| `10h` | SETDMA | the next two `OUT 0Ah`s: the DMA address, low then high | |
| `20h` | FIND FIRST | a pattern (0Bh bytes) | the first matching entry (0Fh bytes) |
| `30h` | FIND NEXT | | the next matching entry |
| `40h` | OPEN | a name (0Bh bytes) | (the file opened to read) |
| `50h` | CREATE | a name | (a new file to write; never over an existing one) |
| `60h` | READ | | the next record (80h bytes); the last padded with `^Z` |
| `70h` | WRITE | a record (80h bytes) | (appended to the file) |
| `80h` | CLOSE | | |
| `90h` | CLOSE TEXT | | (closes, cut at the first `^Z` of the last record) |
| `A0h` | DELETE | a name | |

**`IN 0Ah`: the status** of the last command (below). Like the disk
controller, the device finishes each command during its `OUT`.

The DMA address is kept until the next SETDMA (a reset does not clear
it). Input is read from, and output written to, the memory the Z80 sees
when the command is written: banking applies, as for the disk controller.
Output is only written when the status is 00h.

- **One file and one listing** are open at a time, separately: a listing
  can run while a file is open. OPEN and CREATE close the open file first;
  FIND FIRST starts a new listing; DELETE of the open file closes it.
- **CREATE** makes `/XFER` if it is missing; a file of that name already
  there answers 02h and is left alone (DELETE it first to replace it).
- **READ** answers 03h at the end of the file, with nothing written.
  Files that are not a multiple of 128 bytes come back with the last
  record filled out with `^Z` (1Ah), as CP/M text expects.
- **CLOSE TEXT**, after writing, cuts the file at the first `^Z` in its
  last record, so text written from CP/M does not end in padding. Use
  CLOSE for binary files. On a file opened to read, both just close it.
- **RESET** runs at a machine reset, when the monitor checks the card
  again, and before a firmware update; a program should still CLOSE what
  it wrote, or the file's last records may be lost.

## Data

**Names and patterns** are 11 (0Bh) bytes, in CP/M's FCB form: the name
in bytes 0-7 and the type in bytes 8-10, each left-aligned and padded with
blanks (20h, ␠ below), no dot.

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| `README.TXT` | `R` | `E` | `A` | `D` | `M` | `E` | ␠ | ␠ | `T` | `X` | `T` |
| `*.COM` (pattern) | `?` | `?` | `?` | `?` | `?` | `?` | `?` | `?` | `C` | `O` | `M` |

- Letters are taken in either case and made upper case; bit 7 (CP/M's
  attribute bits) is ignored, so an FCB's name bytes can be passed as
  they are.
- Allowed: ``A-Z 0-9 ! # $ % & ' ( ) - @ ^ ` { } ~``. Blanks only as padding
  at the ends of the name and the type, and the name must not be blank.
  In a pattern, `?` matches any character (there is no `*`: fill the rest
  with `?`, as CP/M does). Anything else answers 06h.
- The file is `/XFER/NAME.EXT` on the volume (`/XFER/NAME` with a blank
  type).

**Listing entries** are 15 (0Fh) bytes:

| Offset | Size | Field |
|---|---|---|
| +0 | 11 | the name, in the form above |
| +11 | 4 | the file's size in bytes, 32-bit little-endian |

Folders, hidden and system files and names CP/M cannot take are left out;
long names appear as their 8.3 short forms (`LONGFI~1.TXT`), which OPEN
takes.

**Records** are 128 (80h) bytes.

## Status

| Value | Meaning |
|---|---|
| 00h | OK |
| 01h | not found: no such file, no `/XFER` folder, or the end of the listing |
| 02h | exists: CREATE found the file there |
| 03h | end of file: READ has no more records |
| 04h | I/O error, or the volume is full |
| 05h | no volume: no card or stick, or no FAT file system on it |
| 06h | bad: a bad name, an unknown command, READ/WRITE with no file open for it |

On picosim, which lacks the device, `IN 0Ah` reads FFh: a program can
tell it apart from every status above.

## Examples

These use one helper that points the DMA address at HL:

```
SETDMA: LD   A,10H        ; SETDMA
        OUT  (0AH),A
        LD   A,L
        OUT  (0AH),A
        LD   A,H
        OUT  (0AH),A
        RET
```

### Reading a file

```
        LD   HL,NAME
        CALL SETDMA
        LD   A,40H        ; OPEN, volume 0 (the SD card)
        OUT  (0AH),A
        IN   A,(0AH)
        OR   A
        JR   NZ,ERROR     ; 01h: no such file
        LD   HL,BUF
        CALL SETDMA       ; the records go to BUF
NEXT:   LD   A,60H        ; READ
        OUT  (0AH),A
        IN   A,(0AH)
        CP   03H          ; end of file?
        JR   Z,DONE
        OR   A
        JR   NZ,ERROR
        ...               ; use the 128 bytes at BUF
        JR   NEXT
DONE:   LD   A,80H        ; CLOSE
        OUT  (0AH),A

NAME:   DB   'README  TXT'
BUF:    DS   128
```

### Writing a file

```
        LD   HL,NAME
        CALL SETDMA
        LD   A,51H        ; CREATE, volume 1 (the USB stick)
        OUT  (0AH),A
        IN   A,(0AH)
        OR   A
        JR   NZ,ERROR     ; 02h: it exists already
        LD   HL,BUF
        CALL SETDMA
LOOP:   ...               ; put the next 128 bytes at BUF
        LD   A,70H        ; WRITE
        OUT  (0AH),A
        IN   A,(0AH)
        OR   A
        JR   NZ,ERROR     ; 04h: the stick is full
        ...               ; more? JR LOOP
        LD   A,90H        ; CLOSE TEXT: a text file, cut at its ^Z
        OUT  (0AH),A
        IN   A,(0AH)
        OR   A
        JR   NZ,ERROR

NAME:   DB   'NOTES   TXT'
```

### Listing the files

```
        LD   HL,BUF       ; the pattern in, each entry out
        CALL SETDMA
        LD   HL,PATTERN
        LD   DE,BUF
        LD   BC,11
        LDIR
        LD   A,20H        ; FIND FIRST, volume 0
NEXT:   OUT  (0AH),A
        IN   A,(0AH)
        OR   A
        JR   NZ,DONE      ; 01h: no more (or none)
        ...               ; BUF: the name (11 bytes), then the size (4)
        LD   A,30H        ; FIND NEXT
        JR   NEXT
DONE:   ...

PATTERN: DB  '????????COM' ; *.COM
BUF:    DS   15
```
