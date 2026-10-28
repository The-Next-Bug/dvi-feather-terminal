# AYTRACK

```
AYTRACK [NAME]
```

A tracker to write tunes for the [sound chip](../../terminal/sound.md) with.
It edits `NAME.AYT` (default `SONG`): a song of up to 16 patterns of 32
rows, each row a note and an instrument on channels A, B and C, played in
the order list's order (up to 64 entries), with 8 instruments and a speed.

The screen shows the pattern, the order list and the instruments; **Tab**
moves between them.

## Pattern

| Key | Action |
|---|---|
| arrows | move |
| `Z S X D C V G B H N J M` | play and enter a note in the octave shown |
| `Q 2 W 3 E R 5 T 6 Y 7 U` | the same, an octave up |
| `[`, `]` | octave down, up |
| `=` | a note off |
| `.`, Delete | clear |
| `0`-`7` (instrument column) | the instrument |
| `^N`, `^P` | show the next, previous pattern |

The table under the instruments shows which key plays which note.

## Orders

| Key | Action |
|---|---|
| Up, Down | select (and show) an entry |
| `0`-`F` | set its pattern |
| `I` | insert a copy after it |
| `X` | delete it |

## Instruments

Each has 16 volume steps (one a frame, the last held), a mode (tone, noise
or both) and a noise period.

| Key | Action |
|---|---|
| arrows | select an instrument and a field |
| `0`-`F` | set a volume step (then the next) |
| `1`, `2`, `3` | the mode: tone, noise, both |
| two digits | the noise period |

The instrument is heard on each change.

## Anywhere

| Key | Action |
|---|---|
| Space | play the song from the selected order, looping (Space again stops) |
| Return | play the pattern on screen, looping (Return again stops) |
| `<`, `>` | the speed: frames a row, at 50 a second |
| `^S` | save `NAME.AYT` |
| `^E` | write the song once through as `NAME.PSG`, for [AYPLAY](ayplay.md) |
| `^Q` | quit (unsaved changes are lost) |

The song file's format is in [File formats](../../reference/file-formats.md#ayt).
