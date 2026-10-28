# AYTEST

```
AYTEST
```

A menu of the [sound chip](../../terminal/sound.md)'s parts. It first looks
for the chip on its ports (D8h/D0h) and says so if there is none (a board
without a DAC, or Sound DAC set to not fitted).

| Item | What it plays |
|---|---|
| 1 | a scale on channel A |
| 2 | chords on A, B and C |
| 3 | a noise sweep |
| 4 | the 16 envelope shapes, over C4 |
| 5 | a volume ramp |
| 6 | A, B and C in turn, saying where each is heard (to hear the stereo layout) |
| 7 | all of them |
| **S** | step the stereo layout: the Stereo setting's, mono, ABC, ACB |

The menu shows the stereo layout in use (none when register 16 reads FFh,
on older firmware). A key stops a part; `Q`, `Esc` or `^C` ends the
program, and the layout AYTEST found is put back. Durations come from the
millisecond tick (ports 68/69); without it AYTEST says so.
