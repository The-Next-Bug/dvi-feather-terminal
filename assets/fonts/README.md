# Fonts

`src/font_gen.py` turns these into the firmware's 256-glyph tables at build
time (see `src/font.h` for the glyph codes).

| File | Used for | Source | Licence |
|---|---|---|---|
| `Uni2-Terminus16.psf.gz` | 80 columns (8x16) | Terminus Font `ter-u16n`, as built by Debian `console-setup-linux` | SIL OFL 1.1, see `Terminus-OFL.txt` |
| `misc-fixed-6x13.bdf` | 132 columns (6x13) | X.org `font/misc-misc` `6x13.bdf` (Markus Kuhn's ucs-fonts) | Public domain ("Public domain font. Share and enjoy.") |
