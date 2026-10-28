# Moving files

Three tools on `CPM22` (and `CPM32` for CP/M 3) move files between CP/M
disks and the folder `/XFER` on the SD card (`SD:`) or a USB stick (`USB:`):
`XLS` lists the folder, `XPIP` copies, `XERA` erases. Files go in and out
without image tools on a PC, and a USB stick can be plugged in and pulled
while CP/M runs. They use the machine's
[file transfer device](../reference/xfer.md), port 10.

## XLS: list

```
XLS [SD:|USB:][afn]
```

Lists `/XFER` with sizes in bytes; `SD:` is the default, and a CP/M
wildcard narrows it:

```
A>XLS
README.TXT         4210
GAME.COM          18432
2 files
A>XLS USB:*.PSG
```

## XPIP: copy

```
XPIP dest=source [T]
```

In the manner of `PIP`: one side is a CP/M drive (`B:`, or none for the
current drive), the other `SD:` or `USB:`.

| Command | Effect |
|---|---|
| `XPIP A:=SD:*.COM` | every `.COM` in `/XFER` to drive A, names kept |
| `XPIP SD:=B:*.TXT` | every `.TXT` on drive B to `/XFER` |
| `XPIP SD:NEW.TXT=OLD.TXT` | one file, renamed |
| `XPIP SD:LOG.TXT=B:LOG.TXT[T]` | a text file: cut at its end-of-file mark |
| `XPIP USB:=B:*.*` | everything on drive B to the USB stick |

- A file of the same name at the destination is replaced, as with `PIP`.
- CP/M files are whole 128-byte records. Copied to CP/M, a file's last
  record is padded with `^Z`; copied to `SD:` or `USB:`, every record is
  kept, and `[T]` cuts the file at the first `^Z` of its last record, as
  text editors on a PC expect.
- Names are 8.3: a PC's long names show as their short forms
  (`LONGFI~1.TXT`), and names CP/M cannot take (such as ones with `_`) are
  not listed.
- Any key stops a copy between files.

## XERA: erase

```
XERA [SD:|USB:]afn
```

Erases files in `/XFER` (`SD:` is the default), naming each. With
wildcards it says how many match and asks first: `Y` erases them, anything
else leaves them. `ERA` erases CP/M files.

## Other ways

- Take the card to a PC and copy `.DSK` images, or files inside them with
  [cpmtools](drives.md).
- `DIR` and `STAT` list the CP/M drives.
