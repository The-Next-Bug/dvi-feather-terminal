# Experimental board headers

Board headers for hardware that is not here and is not expected to be:
taken from the boards' published schematics, never run on one. CI does not
build them, release packages leave them out, and nothing is promised about
them. `test/test_board.sh` still runs `src/board/board.h`'s pin checks on
them, so they keep compiling.

| Target | Board |
|---|---|
| `microfrank` | Stock MicroFRANK (github.com/rh1tech/frank, hardware/microfrank): UART0 on J9 without CTS/RTS, SD on SPI0 GP4-7, the Z80 machine's banks in its 8 MB ESP-PSRAM64H (CS1 GP8, `PSRAM_BANKS`), TDA1387 DAC bell (BCK 10, WS 11, DATA 9), LED GP25, no RTC |
| `picotop-rev2` | 7West PicoTop Rev2 (github.com/7west/PicoTop): UART0 GP0/1 without CTS/RTS (GP2/3 are the charger's STAT lines), SD on SPI0 GP20-23, GP29 the 5 V boost for USB-A and the HDMI +5V; RV-3028 RTC, buzzer, button, card detect and battery not driven |

Building one takes `EXPERIMENTAL=1`:

```bash
EXPERIMENTAL=1 TARGET=microfrank scripts/build.sh
```

To bring a board up for real: move its header to `boards/`, add it to the
CI build matrix and the README's board table, and check `hstx_test.uf2`
first, then the terminal (picture, UART, SD, USB, SET-UP's U).
