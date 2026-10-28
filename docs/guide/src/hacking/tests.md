# Tests and lint

## Host tests

`scripts/test.sh` builds the portable libraries for the host with gcc and
AddressSanitizer/UndefinedBehaviorSanitizer and runs the unit tests in
`test/` with CTest:

| Test | Covers |
|---|---|
| `board` | `board.h`'s checks on every board header and on broken variants |
| `screen`, `palette` | the cell buffer, saved rows, the palettes |
| `vtparse`, `term` | the escape sequence parser and the terminal personalities |
| `raster`, `splash` | lines to pixels at both widths (every pixel of the demo checked), the logo |
| `keyboard`, `blank`, `keyname` | key codes, screen blanking, key names |
| `settings`, `setup` | the settings records, SET-UP and its panels |
| `clock`, `rtc` | dates, the wall clock, the RTC chip drivers on a fake bus |
| `ay` | the AY-3-8910: tones, noise, envelopes, levels, stereo, tunes |
| `xonxoff` | XON/XOFF decisions |
| `sdformat`, `xfer`, `usbvol` | card formats, port 10's files, which stick is `USB:` |
| `uf2`, `lz` | firmware update checks, the image decompressor |
| `machine` | boots CP/M 2.2 and 3 from a RAM volume and runs the tools (TERMTEST, ALTSCR, HWINFO, MEM, XLS/XPIP/XERA, AYTEST, AYPLAY, AYTRACK) and F83 |

`CPU_TESTS=1 scripts/test.sh` also runs the Z80 exercisers (EXZ80DOC,
EXZ80ALL, CPUTEST under CP/M; about 3 minutes, optimized, no sanitizers).

## Lint

`scripts/lint.sh` runs clang-tidy (`.clang-tidy`: braces around every
`if`/`else`/`for`/`while`/`do` body) over the sources as `build/test` and
`build/<target>` compile them, so run the tests and a build first.

## Style

C11, tabs, K&R braces, American spelling; comments explain why. Portable
libraries must not include pico-sdk headers: the host build fails if one
does. A new source goes into its library's `CMakeLists.txt`, a new library
into `src/CMakeLists.txt`.
