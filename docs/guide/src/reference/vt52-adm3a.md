# VT52 and ADM-3A

## VT52 mode

The VT100's own emulation of a VT52 (`src/term/vt52.c`), entered with the
Terminal type VT52 or the host's `CSI ? 2 l`. Control characters act as in
ANSI mode ([VT100](vt100.md#control-characters)).

| Sequence | Action |
|---|---|
| `ESC A` | cursor up |
| `ESC B` | cursor down |
| `ESC C` | cursor right |
| `ESC D` | cursor left |
| `ESC H` | cursor home |
| `ESC Y r c` | cursor to row r - 32, column c - 32 (from the top left; a row out of range leaves the row, a column out of range goes to the last column) |
| `ESC I` | reverse line feed |
| `ESC J` | erase to the end of the screen |
| `ESC K` | erase to the end of the line |
| `ESC F` | line graphics on (DEC Special Graphics) |
| `ESC G` | line graphics off (ASCII) |
| `ESC =` | alternate keypad mode |
| `ESC >` | numeric keypad mode |
| `ESC Z` | identify: replies `ESC / Z` (a VT100 in VT52 mode) |
| `ESC <` | back to ANSI (VT100) mode |

The print controller and hold screen sequences are ignored. The VT100's
G0/G1 character sets are put aside in VT52 mode and come back with
`ESC <`.

## ADM-3A

A Lear Siegler ADM-3A as terminfo's `adm3a` describes it, plus the Kaypro's
additions (`src/term/adm3a.c`). The 8th bit of every byte is ignored (it was
parity; WordStar sets it). No colors, attributes or replies; 80 columns.

| Byte or sequence | Action |
|---|---|
| 07h (^G) | the bell |
| 08h (^H) | cursor left, stopping at the edge |
| 09h (^I) | to the next tab stop |
| 0Ah (^J) | cursor down, scrolling at the bottom |
| 0Bh (^K) | cursor up, stopping at the top |
| 0Ch (^L) | cursor right, stopping at the edge |
| 0Dh (^M) | to column 1 |
| 17h (^W) | erase to the end of the screen (Kaypro) |
| 18h (^X) | erase to the end of the line (Kaypro) |
| 1Ah (^Z) | clear the screen, cursor home |
| 1Eh (^^) | cursor home |
| `ESC = r c` | cursor to row r - 32, column c - 32 (out of range: the nearest edge) |
| `ESC E` | insert a line (Kaypro) |
| `ESC R` | delete a line (Kaypro) |

Printing in the last column wraps at once to the next line (scrolling from
the bottom line). The Kaypro's `ESC A` / `ESC G` (ASCII / Greek) are
ignored.

The keys an ADM-3A sends are in [Keys sent](keys.md#adm-3a).
