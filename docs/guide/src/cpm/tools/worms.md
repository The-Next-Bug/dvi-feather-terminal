# WORMS

```
WORMS [-N n] [-L n] [-D ms] [-F] [-T] [-C] [-M]
```

Worms wandering about the screen, after BSD's worms(6), each in its own
color and glyph. It asks the terminal for the screen size, so it fills 132
columns too. Any key ends it.

| Option | Effect |
|---|---|
| `-N n` | the number of worms, 1-40 (default 3) |
| `-L n` | their length, 2-255 (default 16) |
| `-D ms` | milliseconds a step (default 40; 0: as fast as the Z80 goes) |
| `-F` | a field of dots to eat |
| `-T` | a trail of dots |
| `-C` | 256-color rainbow worms: each head steps round a 30-color hue wheel from the xterm palette, so the bodies are gradients |
| `-M` | no color |

For example `WORMS -N 10 -L 30 -F` or `WORMS -C -N 6 -L 40`.
