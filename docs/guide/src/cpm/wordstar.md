# WordStar 4

The `WS4` disk (built in, 8 MB) holds MicroPro's WordStar 4.00 for
CP/M-80, installed for the VT100: `WS.COM` and its overlays, the spelling
checker (`TW.COM`, `SPELL.COM`, `MAINDICT.CMP`), the other utilities,
`WSCHANGE` to install it again, and the sample documents. `WS80.COM` and
`WSADM.COM` are the same WordStar installed for other terminals; they use
the same overlays.

Sources: `lib/wordstar` (the files), `src/machine/cpm/ws4/ws4_patch.py`
(the patches below), `src/disks/diskimg.cmake` (the disk), `src/ui/monitor.c`
(the `WS4` drive set).

## Running it

The monitor's install (**I**) writes the `WS4` [drive set](drives.md#drive-sets):

| Drive | Disk |
|---|---|
| A: | `CPM31` ([CP/M 3](cpm3.md), boots) |
| B: | `CPM32` |
| C: | `WS4` |
| H: | `RAMDISK` on boards with PSRAM; elsewhere `WSTEMP`, a blank 4 MB disk the install makes |

Pick the set in SET-UP's Drives panel, Return, **F11**, then:

```
A>C:WS LETTER.DOC
```

WordStar looks for its overlays on the current drive, then on C:, so it
runs from any drive as long as `WS4` is on C:. Documents go wherever you
name them (`B:LETTER.DOC`), A: being a nearly full floppy.

## Temporary files on H:

While you edit, WordStar keeps the text that does not fit in memory in
`NAME.$A$` and `NAME.$B$`, and a block being moved in `NAME.$C$`. The
disk's `WS.OVR` makes them on H:, whatever the document's drive, so a long
document is edited on the RAM disk (or `WSTEMP`) rather than on the SD
card, and saving copies the text back to the document's own drive. They
are deleted when the document is closed.

So H: must be mounted while WordStar runs, else it says it cannot create
the temporary files. Nothing of yours is kept there: on the RAM disk it
does not matter that H: is blank after a power cycle. WordStar 4's own
`WS NAME d:`, which put the temporary files on d: and saved the document
there, has no effect with this `WS.OVR`.

If the document's disk fills up during a save, WordStar falls back to
renaming `NAME.$B$` on H: to `NAME`, and goes on editing it there: copy it
off H: (`PIP B:=H:NAME`) before the power goes, as the RAM disk keeps
nothing over a power cycle.

## The patches

WordStar 4 has no setting for a temporary drive, so `ws4_patch.py` changes
four places in `WS.OVR` and one in each `WS*.COM`, checking the original
bytes first (addresses as WordStar runs them):

| Where | Change |
|---|---|
| `WS.OVR` 3B0Dh, making a temporary file | the drive is always H: |
| `WS.OVR` 8A8Eh, saving | always copy `$B$` to the document, never rename it |
| `WS.OVR` 8ABBh-8B01h, the copy | from record 0, a record at a time, up to the record with the ^Z |
| `WS.OVR` 8A91h, after the copy | the document stays on its drive |
| `WS*.COM` LGLDRV | `CAB...` for `ABC...`: WordStar's own files on C: |

`WSCHANGE` starts from the disk's `WS.COM`, so a new installation keeps
the C: search unless you change the legal drives, and the temporary files
stay on H: (that is `WS.OVR`).
