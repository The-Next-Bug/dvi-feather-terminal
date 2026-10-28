# AYPLAY

```
AYPLAY [d:]NAME[.PSG] [MONO|ABC|ACB] [LOOP]
```

Plays PSG files: the [sound chip](../../terminal/sound.md)'s register
writes frame by frame, as emulators record them.

```
A>AYPLAY JACQUES ABC
Playing JACQUES.PSG in ABC stereo (Q stops)
  0:07
```

- `AYPLAY` alone shows the usage and lists the `.PSG` files on the drive.
- `MONO`, `ABC` or `ACB` picks the stereo layout; otherwise the Stereo
  setting's.
- `LOOP` plays it until stopped.
- `Q`, `Esc` or `^C` stops it, quiets the chip and puts back the layout it
  found.

Frames play every 20 ms (50 a second) on the millisecond tick; when it
falls more than 200 ms behind it catches up rather than rushing.

Three tunes come with it, arrangements of public-domain melodies:

| File | Tune |
|---|---|
| `KOROBEIN.PSG` | Korobeiniki (the Tetris theme) |
| `JACQUES.PSG` | Frere Jacques as a round in three voices: in ABC it moves from left to right |
| `ODE.PSG` | Ode to Joy |

PSG files from elsewhere play too, copied in with
[XPIP](../moving-files.md), and [AYTRACK](aytrack.md) exports its songs as
PSG. The format is in [File formats](../../reference/file-formats.md#psg).
