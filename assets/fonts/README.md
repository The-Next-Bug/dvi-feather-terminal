# Fonts

`src/font_gen.py` turns these into the firmware's 256-glyph tables at build
time (see `src/font.h` for the glyph codes).

| File | Used for | Source | License |
|---|---|---|---|
| `Uni2-Terminus20x10.psf.gz` | 80 columns (10x20) | Terminus Font `ter-u20n`, as built by Debian `console-setup-linux` | SIL OFL 1.1, see `Terminus-OFL.txt` |
| `Uni2-TerminusBold20x10.psf.gz` | bold text in 80 columns | Terminus Font `ter-u20b`, as built by Debian `console-setup-linux` | SIL OFL 1.1, see `Terminus-OFL.txt` |
| `Uni2-Terminus24x12.psf.gz` | 80 columns at 1024 px wide (12x24) | Terminus Font `ter-u24n`, as built by Debian `console-setup-linux` | SIL OFL 1.1, see `Terminus-OFL.txt` |
| `Uni2-TerminusBold24x12.psf.gz` | bold text in 80 columns at 1024 px wide | Terminus Font `ter-u24b`, as built by Debian `console-setup-linux` | SIL OFL 1.1, see `Terminus-OFL.txt` |
| `misc-fixed-6x12.bdf` | 132 columns (6x12, shown at double height; at 1024 px wide in 7-pixel cells) | X.org `font/misc-misc` `6x12.bdf` (Markus Kuhn's ucs-fonts) | Public domain ("Public domain terminal emulator font. Share and enjoy.") |
