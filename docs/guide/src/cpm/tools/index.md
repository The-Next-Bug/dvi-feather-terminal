# Tools

This project's own CP/M programs, on the built-in disks:

| Program | What it is | On |
|---|---|---|
| [HWINFO](hwinfo.md) | the firmware, the board and its hardware | `CPM22`, `CPM31`, `CPM32` |
| [MEM](mem.md) | the TPA, the memory banks, and a bank test | `CPM22`, `CPM32` |
| [XLS, XPIP, XERA](../moving-files.md) | files to and from the SD card and USB sticks | `CPM22`, `CPM32` |
| [TERMTEST](termtest.md) | a page-by-page check of the terminal types | `Z80TESTS` |
| [ALTSCR](altscr.md) | a check of the alternate screen | `Z80TESTS` |
| [WORMS](worms.md) | worms wandering about the screen | `CPM22`, `CPM32` |
| [AYTEST](aytest.md) | a menu of the sound chip's parts | `CPM22`, `CPM31`, `CPM32` |
| [AYPLAY](ayplay.md) | plays PSG tunes | `CPM22`, `CPM32` |
| [AYTRACK](aytrack.md) | a tracker to write tunes with | `CPM22`, `CPM32` |

Under CP/M 3 the programs on `CPM32` (drive B:) run from any drive through
the search path. Programs that read keys stop with `Q`, `Esc` or `^C`
unless their page says otherwise.

Their sources are in `src/machine/cpm/` (Z80 assembler, built with
z80pack's `z80asm` when the firmware is built).
