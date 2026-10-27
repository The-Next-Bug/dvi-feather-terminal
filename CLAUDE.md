# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:6cd5cc61 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/SYNC_CONCEPTS.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->


## Build & Test

Everything installs under `.toolchain/` (git-ignored); there is no system
CMake or Arm GCC on the dev host. Use the scripts:

```bash
git submodule update --init          # lib/pico-sdk (2.3.1) and lib/tinyusb, not pico-sdk's submodules (lib/z80pack is vendored)
scripts/setup-toolchain.sh           # pinned CMake, Arm GNU Toolchain, picotool, clang-tidy (toolchain.env)
scripts/test.sh                      # host unit tests (gcc + ASan/UBSan): screen, vtparse, term, raster, keyboard, keyname, settings, setup, splash, clock, ay (the AY-3-8910's tones, noise, envelopes, levels), rtc (the RTC chip drivers on a fake register bus), xonxoff (the XON/XOFF decisions), sdformat (card formats on a sparse disk), uf2, lz, xfer (port 10's files on a FAT12 RAM volume), machine (boots CP/M 2.2 from a FAT32 RAM volume)
CPU_TESTS=1 scripts/test.sh          # also the Z80 exercisers (test_cpu, ~3 min, optimized, no sanitizers): EXZ80DOC, EXZ80ALL and CPUTEST under CP/M; build/test/test_cpu <PROGRAM> runs any program on B:
scripts/build.sh                     # TARGET=pico2-dvi-sock (default; any boards/<target>.h) -> build/<target>/dvi_terminal.uf2
scripts/package.sh                   # every built target -> dist/dvi-terminal-<describe>.zip
scripts/lint.sh                      # clang-tidy (.clang-tidy) over build/test and build/$TARGET's compile_commands.json (build them first)
```

Lint (bd 4rf): `.clang-tidy` enables readability-braces-around-statements
as an error (every if/else/for/while/do body a { } block; else if
allowed). clang-tidy is the static binary from PyPI's clang-tidy wheel
(CLANG_TIDY_* in toolchain.env, installed with clang's headers into
.toolchain/clang-tidy). It sees code under #if only as the build
configures it; CI lints build/test in the test job and each target in its
build job. Spelling is American (bd azw: color, gray, center, license,
-ize), except in third-party texts (assets/fonts/Terminus-OFL.txt).

Boards (bd axq): `boards/<target>.h` is a pico-sdk board header
(PICO_BOARD = target, PICO_BOARD_HEADER_DIRS = boards/) plus every pin the
firmware uses: BOARD_DVI_{CLK,D0,D1,D2}_{P,N}_PIN (any order/polarity in
GP12-19), optional BOARD_DVI_DRIVE_MA / _SLEW_FAST, BOARD_UART (0/1) +
BOARD_UART_{TX,RX,CTS,RTS}_PIN (terminal UART, also VIDEO_DIAG; CTS/RTS
optional; the header points PICO_DEFAULT_UART* at them for hstx_test's
stdio, before including an SDK board), BOARD_SD_SPI + BOARD_SD_{MISO,CS,SCK,MOSI}_PIN,
optional BOARD_USB_VBUS_EN_PIN / _LEVEL (on first thing in main()), optional PICO_DEFAULT_LED_PIN
(src/platform/led.[ch], bd 1ls.8: off while the screen is on; flashes on/off
>= 40 ms each on disk_read/disk_write and serial_read/serial_write data;
with the screen blanked a 100 ms heartbeat each second from led_poll in
the main loop and cpm_idle, so a hang shows as steady on or off; no-op
without the pin or with VIDEO_DIAG, which owns it),
optional BOARD_PSRAM_CS_PIN (XIP_CS1: GP0/8/19/47; without PSRAM_BANKS
main() drives it high first thing so the PSRAM stays off the shared QSPI
bus; with it, PICO_PSRAM_CS_PIN + PICO_AUTO_DETECT_PSRAM_SIZE in the header
let the SDK's runtime init set it up),
optional BOARD_RTC_DS3231 or BOARD_RTC_PCF8523 (one: BOARD_HAS_RTC;
bd 1ls.19.3) + BOARD_RTC_I2C + BOARD_RTC_SDA/SCL_PIN (GPIO n
is I2C (n >> 1) & 1, SDA even, SCL odd) and BOARD_RTC_SQW_PIN (the 1 Hz
tick, DS3231 only, needs a chip; bd 1ls.6.1, 1ls.6.2; HWINFO's record
names the chip, rtc_chip_name, and says "battery low", rtc_battery_low), optional BOARD_AUDIO_I2S_{BCK,WS,DATA}_PIN (WS
= BCK + 1: PIO side-set) + _NAME (src/platform/bell.c, bd 1ls.7), and the
planned hardware's pins, named before any driver exists (bd 1ls.4):
BOARD_NEOPIXEL_PIN. All count in board.h's used-once check. Dropped
hardware (bd 1ls.22: BOARD_BUZZER_PIN, BOARD_SD_CD_PIN, BOARD_BUTTON_PIN,
BOARD_BATTERY_ADC_PIN, BOARD_CHARGER_STAT_PIN) is an #error in board.h, its
HWINFO items 29-31 retired; dropping a feature later: remove its record,
pin bits, checks and docs the same way. apps/terminal/hwinfo_glue.c gives no record
(0 bytes, HWINFO skips it) for hardware the header does not name, PLANNED
for what it names without a driver.
`src/board/board.h` (RP2350 code only) #errors on missing settings, wrong
function pins, pins used twice or off the chip; test/test_board.sh (ctest
`board`) runs it on every board header and on broken variants. Pin values
must be plain numbers (they are stringified into on-screen messages).
`boards/example.h` is the documented template (same pins as the Sock, and
standalone: RP2350A, flash, crystal). The user is designing a custom board.
`boards/frankless.h` (bd 59u.1): that board, the user's reworked MicroFRANK
(github.com/arlaneenalra/pi-pico-boards, reworked/frankless; reviewed in bd
59u), from its KiCad files, partly tried on hardware: RP2350A, 16 MB flash,
12 MHz oscillator on XIN, HDMI GP12-19 (CLK 13/12, D0 15/14, D1 17/16, D2
19/18), UART1 GP20-23 with CTS/RTS (header J8), SD on SPI0 MISO 4 / CS 1 /
SCK 2 / MOSI 3, APS6404L PSRAM CS on GP0 with PSRAM_BANKS (the Feather
PSRAM build's mechanism; bd 1ls.3.1.1), LED GP9; DS3231M RTC on I2C1
GP10/11 (BOARD_RTC_DS3231, bd 1ls.6.1) with its 1 Hz on GP8 (the clock's
tick); TDA1387 DAC (U8, hand-fitted) on GP5 BCK / 6 WS / 7 DATA for the
bell. Slide switch S1 puts the USB
on the hub (host) or the USB-C socket (BOOTSEL); hub VBUS is always on.
CI builds it.
`boards/feather-rp2350-hstx.h` (bd 1ls.14.1): Adafruit Feather RP2350 with
HSTX Port on the SDK's adafruit_feather_rp2350.h (8 MB flash, LED GP7,
PSRAM CS GP8 on some), DVI from the 22-pin FPC per Adafruit's
CircuitPython pins.c (CLK 14/15, D0 18/19, D1 16/17, D2 12/13, + even),
UART0 GP0/1 (CTS/RTS GP2/3 = STEMMA QT SDA/SCL), SD SPI0 MISO 20 / SCK 22 /
MOSI 23 / CS 10 (Adalogger FeatherWing); run on the user's Feather; CI builds it.
`boards/feather-rp2350-hstx-psram.h` (bd 1ls.17): the user's Feather with
an APS6404L 8 MB PSRAM on CS1 GP8; `pico_board_cmake_set(PSRAM_BANKS, 1)`
plus the SDK header's three CMake lines restated (CMake follows only
`#include "boards/..."`, which the compiler cannot find from boards/; the
base header is included relatively). PSRAM_BANKS: MACHINE_BANKS 0 (96 KB
of SRAM freed), the program links hardware_psram (runtime init finds and
sets up the chip at boot, at the boot clock; main() then leaves GP8 alone),
`src/platform/extram.[ch]`: extram_retime() after video_init and before
nvflash_retime (psram_reinitialize goes through flash_start_xip, which
leaves the flash's XIP set up for <= 150 MHz), divisor 4 at 400 MHz;
cpm mode gives the machine extram_base() (0x11000000, cached, writable)
as min(15, size / 48 KB) banks (machine_set_banks) and says so under the
banner. The PSRAM is handed out in order by extram_take (banks first);
the RAM disk (bd 1ls.20) takes last from extram_left the largest format
that fits (drives_format_fit: HD4 on 8 MB), so a new PSRAM user goes
between them and the RAM disk shrinks. drives.c: drives_set_ramdisk
blanks it (0xE5); RAMDISK (DRIVES_RAMDISK) is a reserved image name
(mounted from memory, listed by drives_list, never a card file, not
made by drives_create), kept over unmounts, blank after a reboot. nvflash_retime saves and restores QMI M1 around the ROM's
flash_select_xip_read_mode; the SDK's flash_range_* clean the XIP cache
(dirty PSRAM lines) and re-run the PSRAM setup themselves. Core 1 never
touches the PSRAM. The Z80 banks in PSRAM work on the user's Feather (bd
1ls.17, 2026-10-25); CI builds it.
`boards/feather-rp2350-hstx-adalogger.h` and
`boards/feather-rp2350-hstx-psram-adalogger.h` (bd 1ls.19): the user's
Feather (PSRAM fitted) with an Adalogger FeatherWing: the base header
(and the PSRAM one) plus BOARD_RTC_PCF8523 on I2C1 GP2/3 (the Feather's
SDA/SCL), so BOARD_UART_CTS/RTS_PIN are #undef'd (BOARD_HAS_RTSCTS 0: no
RTS/CTS in SET-UP; test_board.sh asks the preprocessor which boards lack
CTS and checks BOARD_HAS_RTSCTS against it); the
pico_board_cmake_set lines restated (and PSRAM_BANKS). No 1 Hz pin: the
FeatherWing's INT/SQW pad is not wired. Brought up on the user's Feather
(bd 1ls.19.6); CI builds both.
`boards/experimental/` (README.md there): headers for boards the user does
not and will not have, from schematics, never run: `microfrank.h` (stock
MicroFRANK, bd 1ls.3.2/59u: UART0 GP0/1 no CTS/RTS, SD SPI0 GP4-7, PSRAM_BANKS
with ESP-PSRAM64H CS1 GP8, TDA1387 BCK 10 / WS 11 / DATA 9 per
frank-kickstart BOARD_M2, LED 25, no RTC) and `picotop-rev2.h` (bd c0j:
UART0 GP0/1 no CTS/RTS, SD SPI0 20-23, BOARD_USB_VBUS_EN_PIN 29 also the
HDMI +5V, which main() turns on before video_init, bd gy1; buzzer 10,
button 11, SD CD 24, battery ADC 28, charger STAT 2/3 and the RV-3028 on
I2C1 6/7 only in its comments: no drivers, those features dropped). Not in CI's matrix;
scripts/build.sh refuses them without EXPERIMENTAL=1 and writes
experimental=yes into build-info.txt, which package.sh skips; CMake finds
them through PICO_BOARD_HEADER_DIRS and warns; test_board.sh checks them.

The USB host (keyboards, sticks) is always built in (USB_KEYBOARD=OFF
was removed: bd s0i), and flow control is a setting (bd dmt.27: the
SERIAL_FLOW_CONTROL option is gone). Build options (CMake; `scripts/build.sh`
passes them via `CMAKE_ARGS`): `STARTUP_LOGO`
(default ON; image from `LOGO_IMAGE`, shown `LOGO_MS`, default 10000: monitors
need seconds to sync), `LOGO_IN_FLASH` (default ON, experimental dmt.25: logo data in a
`.flashdata` section read via XIP after `nvflash_retime()`; frees ~19 KB of
SRAM), `STARTUP_DEMO` (default OFF),
`VIDEO_DIAG` (UART0 TX diagnostics, default OFF; also prints
panic() text),
`COPY_TO_RAM` (default ON; must stay ON, see below), `MACHINE_BANKS` (Z80
machine: 48 KB banks besides bank 0, default 2; each costs 48 KB of SRAM).
TinyUSB workaround switches (bd nxv; default OFF since 2026-10-25 = rely
on the fork's fixes; ON builds ours back in; dvt_usb only, usbkbd.c;
USB_WA_POLL_PAUSE defaults to ON again since 2026-10-26, bd 008: without
it a stick read during a firmware update came back stale, badcsw and
"blocks out of order"; the cache entry follows each switch's default via
its help text): USB_WA_DEINIT
(--wrap=rp2usb_deinit, remove-before-deinit in usbkbd_reset; #4310),
USB_WA_DATASEQ (INTE mask in host_start, usbkbd_data_seq_error's raw INTR
check + restart, which then returns false; #4312), USB_WA_POLL_PAUSE
(usbkbd_polls_pause/resume become no-ops; #4313), USB_WA_CONTROL_WAIT
(control_wait returns at once); USB_RESTART_KEY (default OFF): H in SET-UP
calls usbkbd_request_reset("hkey"). build-info.txt's usb_workarounds line
records them. Test images: BUILD_DIR=build/wa-<variant> CMAKE_ARGS=...

Always run `scripts/test.sh`, a firmware build and `scripts/lint.sh` before handing work
back. Hardware checks are done by the user (no board is attached to the
dev host; Docker is not usable for CI simulation).

## Architecture Overview

VT100 serial terminal with DVI output for a Pico 2 (RP2350) with the Pico
DVI Sock. RP2040 (Feather RP2040 DVI) support was dropped (dmt.29): out of
SRAM, and the USB keyboard crashed it (dmt.20); CMake rejects non-RP2350
boards. 800x600p60 (400 MHz, 1.3 V); 80 or 132 columns x 24 rows plus a
status line (row 24); UART0 on GP0/GP1.

Source layout (bd gr1, after github.com/arlaneenalra/CGA_LCD_Capture):
libraries under `src/<name>/`, each a static `dvt_<name>` with its own
CMakeLists.txt (`src/CMakeLists.txt` lists them); public headers next to
the sources (the directory is the PUBLIC include path), a header only the
library's own files share is `<name>_internal.h` (term_internal.h).
Portable (host-tested): common, clock, sound, screen, term, render, input, settings,
machine (+ dvt_z80pack, cpm/), disks, ui, rtc's dvt_rtc_chips; RP2350, added only with
DVT_FIRMWARE: board (header only), platform, usb, storage, video, rtc's
dvt_rtc (a directory may hold both, the RP2350 one under if(DVT_FIRMWARE)). The
host tests add the same `src/` without DVT_FIRMWARE, so a pico-sdk
include in a portable library fails test.sh. RP2350 libraries link the
SDK's `*_headers` targets (TinyUSB has none: usb/'s dvt_tinyusb_headers
copies its include dirs and definitions); only the programs link SDK
libraries, so no SDK source is built into an archive. Libraries get
DVT_C_OPTIONS (firmware: pico_standard_link's -ffunction-sections
-fdata-sections, without which --gc-sections keeps unused functions in
SRAM, and -fno-math-errno; tests: sanitizers) and our warnings plus
DVT_WARNING_OPTIONS (tests: -Werror) via `cmake/dvt.cmake`
dvt_compile_options (THIRD_PARTY: not the warnings); dvt_rebuild builds
libraries' sources into another target (test_cpu's -O2 copies). The
firmware's options reach libraries from the top CMakeLists
(FONT_FLASH, LOGO_IN_FLASH, DISKIMG_IN_FLASH, MACHINE_BANKS on
dvt_z80pack, VIDEO_DIAG on
dvt_platform, HSTX_MODE on dvt_video). FatFs is the `fatfs` target
(lib/fatfs.cmake), linking dvt_storage for disk_* (a cycle CMake repeats
on the link line; tests add diskio_ram.c to the tests that need it).
Programs: `apps/terminal/` (main.c: state, settings, startup, main loop;
setup_glue.c: SET-UP and its panels and demo; cpm_glue.c: CP/M mode's
platforms; status.c: the status line; input_glue.c: keys; terminal.h
shares them) and `apps/hstx_test/`; both write their outputs to
build/<target>/ (RUNTIME_ and ARCHIVE_OUTPUT_DIRECTORY: picotool puts the
UF2 in the latter).

- `src/screen/screen.[ch]` (portable): cell buffer: glyph code, fg/bg palette
  indices and attribute bits per cell; `row_map` (display row -> line slot) so
  scrolling rotates indices; `dirty` bit per slot; `reverse_rows` mask.
  A scroll clears the recycled slot's cells, then calls the screen's
  recycle hook, then remaps it (bd ful): video.c's hook blanks that slot's
  raster at once (raster_blank_line, the row's color, reverse included),
  since core 1 draws raster[], which only catches up at video_update.
  screen_stash_t (bd qur.16): saved rows as tagged entries in one
  owner-provided buffer (4 bytes a cell at the width then; put back or
  dropped in any order, wider screens padded): main.c's 14.1 KB stash_buf
  holds the alternate screen's main screen (24 rows x 132) or that plus
  SET-UP's rows at 80 columns; at 132, SET-UP over the alternate screen
  has no room and its rows come back blank.
- `src/term/vtparse.[ch]` (portable): Paul Williams' DEC parser. C1 (0x80-0x9F)
  ignored unless `c1_controls` (keeps UTF-8 safe); OSC/DCS/APC discarded,
  and ended by any C0 but NUL (BEL in OSC is the terminator), which then
  executes, or after VTPARSE_MAX_STRING (1024) bytes (dmt.36, xterm
  brokenStringTerm: an Alt+] sends ESC ]).
- `src/term/term.[ch]` (portable): the terminal, split by personality (bd
  dmt.37) so each terminal type's meaning of a byte stays in its own file:
  term.c is the cell-level core and the public API (cursor, print with
  charsets and deferred wrap, scroll region, erase, IL/DL, tabs, reverse
  screen, 80/132, replies), its helpers in `src/term/term_internal.h` (for
  personalities only); term_write/term_reset dispatch on term_t.personality.
  `src/term/vt100.c`: the VT100/VT102 personality (vtparse, C0/ESC/CSI, modes,
  SGR; its state in term_t.vt100: parser, DECSC, DECCKM/DECKPAM, REP;
  CSI ? 47/1047/1049 h/l via term.c term_set_alt_screen (bd dmt.15): the
  main screen goes into term_set_stash's stash (tag TERM_STASH_TAG), the
  alternate starts blank, 1049 adds DECSC/DECRC; RIS drops the entry);
  replies (DA, DSR/CPR, DECREQTPARM) through a callback. `src/term/vt52.c`
  (dmt.16): the VT100's VT52 mode (CSI ? 2 l in, ESC < out; own small
  parser for ESC Y row col; ESC Z -> ESC / Z; ANSI G0/G1 put aside),
  keyboard.c sends ESC x / ESC ? x when kbd_modes_t.vt52 (keys a VT52
  lacks, Home to Page Down and F5-F11, send nothing). `src/term/adm3a.c`
  (dmt.28): the ADM-3A personality per terminfo adm3a (^H ^J ^K ^L moves
  stopping at edges, ^Z clear, ^^ home, ESC = row+32 col+32 clamped,
  immediate wrap, 8th bit stripped, no replies, 80 columns only) plus the
  Kaypro's ^X/^W clear EOL/EOS and ESC E/R insert/delete line; keys
  ^K ^J ^L ^H, Home ^^ (kbd_modes_t.adm3a). Changing personality
  (term_set_defaults) resets the terminal. SET-UP and the CP/M monitor
  always get VT100 ANSI-mode keys (input_glue.c keyboard_poll), whatever the
  host's modes; the host cannot open SET-UP (CSI ? 999 ~) from the ADM-3A.
  Writes palette indices
  only (nothing depends on the palette): default fg 7, bold default 15, bg
  0; bold brightens 0-7 to 8-15; 38;5;n as is; 24-bit -> palette_nearest.
- `src/screen/palette.[ch]` (portable, bd pb2): the xterm-256 palette as RGB565
  from Colors (hue) x Palette (mono): white/color = our ANSI set + xterm
  16-255; phosphor/color = 0-15 a ramp in the hue by bands (7 at 2/3, 15
  full) + xterm 16-255; mono = all 256 as a ramp by luma (floor 35%).
  main.c show_palette() -> video_set_palette (two 256-word tables, core 1
  switches at frame start). A palette change re-renders nothing. While
  SET-UP is open the edited palette is shown (restored on cancel).
- `src/render/raster.[ch]` (portable): two layouts (raster_layout_t: 80 cols
  10x20 at the full width, 132 cols 6x12 doubled; the 8x16 centered layout
  and its setting were dropped in qur.20). `raster_render_line` (core 0)
  turns a screen line into, per stored row, a bit string of the cells'
  pixels (cell c from bit c * width, 100 bytes per row in both layouts;
  raster_cell_bits reads one) plus per-cell fg/bg (palette indices, reverse
  applied). It writes into the slot core 1 reads, replacing each cell's
  bits under a mask (no clearing first: a memset there blanked lines
  mid-render, qur.20). Its fonts: raster_select_font copies the layout's
  normal and bold tables from flash into one SRAM pair of 16-bit rows
  (font_copy, 20 KB) on a layout change (video_update calls it before
  timing renders); raster_glyph_row reads the flash tables (tests).
  Stored rows per line = font rows + 2 padding rows; connectors (line
  drawing, blocks, shades) extend into the padding. Handles
  underline, blink phase, bold (bold face; 132 cols: color only),
  reverse. `raster_compose` (core 1) makes a scanline of RGB565 pixel pairs
  from a stored row through the 256-entry palette table, inverting the
  cursor cell; every cell is exact in every layout. 132 cols centered at
  x = 4. Core 1's helpers are ternaries, no tables (GCC's CSWTCH tables
  land in RAM, but check the .dis when changing them).
- `src/video/hstx_dvi.[ch]` (RP2350): DVI from the HSTX on the DVI Sock pins
  (GP12-19 = HSTX bits 0-7; the Sock: D0 0/1, CK 2/3, D2 4/5, D1 6/7;
  per-pin from the board header, - = + inverted). clk_sys = 10 x
  pixel clock, clk_hstx = clk_sys / 2 (set explicitly: the SDK leaves it
  undivided from clk_sys); RGB565 through the TMDS expander; two chained
  DMA channels, reloaded by DMA_IRQ_0 with blanking command lists and
  per-line pixels from a callback. `HSTX_MODE` 0 = 800x600@60 with libdvi's
  timing (clk_hstx 200 MHz, above the 150 MHz rating: dmt.8.1), 1 = @56,
  2 = CVT-RB (354 MHz), 3 = 1024x600 CVT-RB (bd cpx.4, experimental
  branch only: 44 MHz, clk_hstx 220 from PLL_SYS 440, clk_sys 384 from
  PLL_USB at 1.30 V (432 needed 1.35 V), clk_usb/adc /8, clk_peri /3 =
  128 MHz: its divider has 2 bits, 1-3; dvt_render PUBLIC
  RASTER_ACTIVE_WIDTH=1024: 80 cols in 12-px cells of Terminus 12x24
  (960 px), 132 in 7-px cells of font_132_7 (6x12 + a column font_gen.py
  --pad-x extends for line drawing; 924 px; raster_compose draws odd
  widths two cells at a time); test_raster_1024 runs test_raster.c at
  that width). The cache option is empty by default: the board
  header's pico_board_cmake_set(BOARD_HSTX_MODE, n), else 0 (top
  CMakeLists: DVT_HSTX_MODE, HSTX_MODE_USED for build-info.txt; a cache
  with the old help string is reset to empty so a stale 0 does not win).
  The Feather headers set 2 (bd 1ls.19.8): at 400 MHz / 1.3 V two
  Feathers hard faulted at random on either core (quad or serial flash,
  PSRAM at 100 or 67 MHz, or none: likely their core supply), stable at
  354 MHz; 132 columns peaks at 98-99% of core 1's line, late 0. The user
  chose that over a voltage bump past 1.30 V. `apps/hstx_test/hstx_test.c` -> `hstx_test.uf2`: test pattern.
- `src/video/video.[ch]` (RP2350): core 1 composes scanlines (raster_compose or
  splash_compose) into a ring of 4 line buffers, at most 2 lines ahead of
  the DMA interrupt (also on core 1), latching mode/cursor/splash per
  frame; counts late lines. `video_update()` on core 0 rasterizes dirty
  slots and the blink phase.
- `src/input/keyboard.[ch]` (portable): HID boot keyboard reports -> VT100 key
  codes (DECCKM, DECKPAM, LNM, Num/Caps Lock, autorepeat via kbd_tick).
- `src/usb/usbkbd.[ch]` + `src/usb/tusb_config.h` (always built): TinyUSB host on the native USB port, boot keyboards (and USB
  sticks, usbmsc.c); keys
  go to the serial TX queue. The board does not power VBUS in host mode
  (5 V fed into the VBUS pin). TinyUSB is `lib/tinyusb` (our submodule,
  for now github.com/arlaneenalra/tinyusb's dvt/integration: upstream
  master after 0.21.0 plus our four proposed hcd/usbh fixes, one fork
  branch each, until upstream has them: bd 5bc's notes say what to watch
  and how to switch back; before that pinned to upstream master, bd xv0),
  not pico-sdk's 0.18: its
  hcd_rp2040 never completed a transfer the device stopped answering and
  usbh had no control-transfer timeout, so pulling a stick could leave the
  host deaf for good. The USB host watchdog (usbkbd_poll, bd xv0): a device
  set up for over 20 s, or a GET_STATUS probe to the hub (else the first
  device) every 5 s while quiet failing 3 times or not completing in 10 s,
  restarts the host (usbkbd_reset: a root-port remove event handled by
  tuh_task first, then tusb_deinit/tusb_init; the unmount callbacks clear
  our state, usbmsc_poll unmounts USB:); each restart doubles the delays
  (up to x16) until 60 s healthy. usbmsc.c asks for a restart
  (usbkbd_request_reset, done from usbkbd_poll; one within 10 s x backoff
  of the last waits, "rstwait") after a stick command times out or gets a
  CSW that is not its own (signature/tag; residue 0 required): TinyUSB has
  no Bulk-Only reset, the stick stays stuck. Workarounds for TinyUSB
  master on the RP2350, all in our code (bd xv0, upstream report bd 5bc;
  since 2026-10-25 built only with their USB_WA_* switch ON, as the fork
  has the fixes, except the INT_EP_CTRL pause, ON by default again (bd
  008); the INT_EP_CTRL pause, control_wait, the data-seq mask
  and the deinit wrap below; CFG_TUH_ENUM_ATTEMPT_MAX, the watchdog and
  the stick checks stay):
  tuh_deinit frees hcd_rp2040's spin lock (rp2usb_deinit) before removing
  the devices, whose hcd_device_close then spun forever with interrupts
  off (--wrap=rp2usb_deinit in src/usb/CMakeLists.txt, usbkbd_reset frees
  it; hcd_deinit itself cannot be wrapped: usbh.c has a weak twin); an EPX
  transfer started while the controller polls the interrupt endpoints by
  itself (hub, keyboard) "completed" at once with 0 bytes (a stick's CBW;
  measured: 15 us, buffer control written back 0x4000), so every EPX
  transfer of ours runs with INT_EP_CTRL cleared for two SOFs first
  (usbkbd_polls_pause/resume: usbmsc.c's commands per FatFs call, the
  LEDs, the probe; ~2 ms each) and our control transfers finish before
  usbkbd_poll returns (control_wait, 20 ms max); CFG_TUH_ENUM_ATTEMPT_MAX 5
  (a keyboard missed its first requests after a restart); hcd_rp2040's
  IRQ panics on a data sequence error (seen re-plugging a stick after
  hours, bd qo3), so host_start clears INTE's ERROR_DATA_SEQ and
  usbkbd_data_seq_error (usbkbd_poll, usbmsc's io_wait) finds the raw
  INTR bit, fails the stick command and requests a restart ("dataseq",
  "seqrst"); crash_glue.c keeps a panic's or hard fault's report (the
  STUCK reports used to overwrite it). TinyUSB's own
  control transfers (enumeration, the hub) are not paused: a device that
  fails to finish being set up is caught by the watchdog. The text log: usbkbd_log
  events plus TinyUSB's CFG_TUSB_DEBUG=1 messages (the top CMakeLists.txt
  sets LOG 1; CFG_TUSB_DEBUG_PRINTF = usbkbd_tusb_printf, also called from
  its IRQ, so the 2 KB buffer is touched with interrupts off), timestamped
  CR LF lines; `apps/terminal/usblog_glue.c` (cold) appends it every 2 s
  from the main loop to 0:/USBLOG.TXT (created if missing) while the saved
  usb_log setting is SETTINGS_USB_LOG_SD, else drops it (terminal mode
  mounts the card for it, at most once a minute; CP/M mode only uses the
  monitor's mount; text waits until the card is mounted). The text and its
  length survive a crash's reboot (__uninitialized_ram, TEXT_MAGIC;
  usbkbd_log_restore). `apps/terminal/crash_glue.c` (SRAM, bd xv0): there
  is no stdio, so panic() (--wrap=panic), a hard fault (isr_hardfault) and
  a hang are reported on the status line: TIMER0 alarm routed to core 0's
  NMI (EPPB NMI_MASK, gets through interrupts off) checks crash_heartbeat
  (keyboard_poll) every 250 ms; after 8 s still it shows STUCK with the
  interrupted PC/LR/exception and the USB registers each second (decode
  with addr2line against the same build's .elf); panic, hard fault, or 20 s
  stuck inside an exception reboot after 20 s, keeping the report
  (__uninitialized_ram) for the next start's banner and USBLOG.TXT. It
  stands down while nvflash_busy (erase/program, XIP off) and fwupdate
  clears the NMI mask before the commit. Long blocking operations (card
  format, the update's file check) show STUCK harmlessly.
- `src/settings/settings.[ch]` (portable): settings record (magic, seq, CRC-32,
  one byte per setting, append-only) in two flash sectors used in turn.
  Settings (settings_fields[] drives the record, validation and SET-UP
  labels; append only): columns, backspace (DECBKM RIS value; default BS
  0x08, as CP/M wants), local_echo,
  baud, framing, newline (LNM RIS value), colors (hue), palette (mono),
  mode (terminal / CP/M machine), status_line (hidden: status.c
  status_shown() leaves row 24 blank; debug: status.c debug_status_* shows
  video_stats() (late lines, compose cycles as % of the line, fps) and
  core 0's longest main-loop pass and line render, qur.21; debug usb:
  uptime and the last four USB host events from usbkbd_log, qur.3), blank_after (minutes; `src/input/blank.[ch]`
  portable idle timer, keys only, the waking key dropped; video_set_blank
  makes core 1 stop the DVI signal: hstx_dvi_stop (DMA aborted, FIFO
  drained, HSTX off), frame time kept on the timer (diag_frames,
  palette_used) for core 0's waits, hstx_dvi_resume at a frame start on
  wake; bd 3rk), term_type (VT100 0 / VT52 2 / ADM-3A 3: personality and
  power-on/RIS mode; byte 11 once held the retired layout, so 1 is never a
  value; then the status line clock's clock (24 / 12 / 0 off),
  clock_seconds, clock_date (SETTINGS_DATE_*), bd 1ls.6.5: after
  SETTINGS_MAIN_FIELDS, edited only on the Clock panel (settings_field_t
  .page: SETTINGS_PAGE_CLOCK); then bell (level 0 off to SETTINGS_BELL_MAX
  10, default 3, bd 1ls.18.1; the old off/low/medium/high were 0-3, so old
  records stay at or below the default; on the Clock and sound panel since
  1ls.18.5), usb_log (field 16: SETTINGS_USB_LOG_OFF/_SD, default off, bd
  xv0), sound_level (the AY's, 0-10, default 7) and sound_dac (1 fitted,
  default; 0: audio.c gives the PIO, DMA, IRQ and pins back, the AY's ports
  read 0xFF), sound_stereo (field 19, SETTINGS_STEREO_* = AY_STEREO_*: mono
  default, ABC, ACB; bd 1ls.18.9; audio_set_stereo from apply_settings),
  all on the panel; flow (field 20, SETTINGS_FLOW_NONE default / _RTSCTS /
  _XONXOFF, bd dmt.27; main.c apply_line -> serial_set_flow, both modes;
  setup_t.no_rtscts = !BOARD_HAS_RTSCTS, from setup_glue.c, makes SET-UP
  skip and hide RTS/CTS). SET-UP's rows (SETUP_ITEMS =
  SETTINGS_MAIN_FIELDS, 14) are settings.c's main_rows table,
  settings_main_field(row) -> field: fields 0-4, flow, fields 5-11,
  usb_log (the preview goes under Palette's row, found by field; tests
  use test_setup.c's row_of); settings_columns() is 80 for the ADM-3A;
  settings_field_fixed() names what fixes a field, Columns under the
  ADM-3A: SET-UP skips it and shows "80 (ADM-3A)", dmt.39);
  settings_colors() gives the status
  line's indices and the logo's tint. CRC-32 in
  `src/common/crc32.[ch]` (shared with the disk images). main.c apply_settings()
  -> serial_configure, term_set_defaults, show_palette.
  `src/ui/setup.[ch]` (portable): SET-UP panel over the bottom text rows,
  in a box (screen_frame: DEC line glyphs in the status bg color, text
  in the default colors; also the Drives panel) (one row per setting, plus two preview rows under Palette: ANSI colors
  as fg and as bg, spaced so no 8-px group held two samples in 132 cols
  under the old PicoDVI encoder)
  (key bytes in, save/cancel out); setup_glue.c saves/restores those rows in
  the stash (tag 'S'; the demo drops them, the screen is cleared). Opened
  by F12 or Pause (keyboard.c SET-UP callback) or host `CSI ? 999 ~` (term.c bumps
  `setup_requests` and term_write stops after it; later bytes are keys).
- `src/input/keyname.[ch]` (portable): key bytes -> "ESC [A", "^C", ... for the
  status line's last-key field (which replaced the rx/tx/err counters).
- `src/platform/nvflash.[ch]`: the 2 flash sectors before the last (UF2 files' E10
  workaround block at 0x10ffff00 wraps to the last sector and erases it). Read at boot before
  video_init; `nvflash_retime()` after the clock change. SDK
  flash_range_* (ROM uses QMI clkdiv 12) + `rom_flash_select_xip_read_mode`
  03h at <=50 MHz afterwards (the boot XIP stub the SDK replays assumes
  <=150 MHz). No flash_safe_execute (core 1 in RAM). Quad reads (bd cpx.2,
  BOARD_FLASH_QUAD, default 1): nvflash_init sets the flash's QE bit
  (35h, then volatile 50h + 31h); nvflash_retime reads 16 words at 03h,
  switches to EBh (QUAD_SCK_MAX_HZ, 50 MHz to start) and keeps it only if
  they read the same, else a status read via flash_do_cmd (exit sequence,
  CS1 PSRAM restored) and back to 03h; nvflash_quad()/_read_hz() for
  HWINFO's CPU line.
- Clock (bd 1ls.6.1): `src/clock/` (portable, test_clock): datetime.[ch]
  (seconds since 31.12.1977 = CP/M 3's day number x 86400, civil
  conversions without localtime, FAT timestamps; DATETIME_DEFAULT_DAYS
  17533 = 1.1.2026). `src/rtc/` (bd 1ls.19.1): portable dvt_rtc_chips
  (test_rtc, fake register bus): rtc_chip.h (rtc_bus_t read/write by
  address and register through function pointers; rtc_chip_t name,
  start = set up + read -> MISSING / NOT_SET / OK, set (false outside its
  years), optional start_1hz, output falling as the seconds go up), one
  file per chip: ds3231.[ch] (register codec, 24 h, century bit, driver
  rtc_ds3231), pcf8523.[ch] (bd 1ls.19.2: time at 03h, OS in the seconds,
  weekday 0-6, 2000-2099, 12-hour mode from Control_1's 12_24; start
  clears Control_3's PM to 000, since the power-on 111 has no battery
  switch-over, and sets CLKOUT off, COF 111; NOT_SET also when STOP; set
  clears STOP/12_24; battery_low from BLF; no 1 Hz yet), optional
  battery_low, BCD in rtc_internal.h. RP2350 dvt_rtc: rtc.[ch] below.
  wallclock.[ch]: a second at a timestamp; wallclock_edge (the chip's
  seconds went up) moves it to the edge, rounding for missed edges,
  ignoring glitches < 0.5 s, never back. `src/rtc/rtc.[ch]`: the I2C bus
  for the chip the board header names (RTC_CHIP), read once in rtc_init (after nvflash_retime; I2C 100 kHz, 5 ms
  timeouts), then with BOARD_RTC_SQW_PIN the chip's start_1hz (DS3231:
  INTCN, RS, EOSC cleared) and a raw GPIO IRQ on its
  falling edge (bd 1ls.6.2) feeding wallclock_edge, else time_us_64;
  readers disable interrupts around the wallclock. OSF set or no answer ->
  1.1.2026; rtc_set writes the chip and clears OSF (always kept for the
  session); rtc_state / rtc_ticking for HWINFO's record, rtc_valid (read
  OK or set since) gates status.c's clock (bd 1ls.6.3: " HH:MM " in the
  last 7 columns of both modes' status lines, colon on while
  rtc_fraction_us < 500 ms; status_clock_poll each main-loop pass redraws
  only its cells on a minute*2+colon change; put_status_text cuts the
  line's text before it; its text and width from datetime_clock_text with
  the clock settings, 7-24 columns). Users: cpm_glue's machine platform
  (time_get/time_set), get_fattime. SET-UP's C, the Clock and sound panel:
  `src/ui/clockpanel.[ch]` (portable, cold, test_setup; bd 1ls.6.4,
  1ls.18.5; 17 rows): Date/Time follow rtc_now until digits are typed over
  them (fixed positions, no sscanf), the three clock settings as choices, a
  sample row, then Bell, Sound chip, Stereo and Sound DAC (field_of's table;
  without a DAC on the board, has_dac, all four fixed, Sound DAC not fitted
  the first three, shown off and skipped); Return -> setup_glue.c clock_service
  (main loop: rtc_set if typed, the settings into setup.ui.values for
  SET-UP's save). setup_service previews the panel's Bell (rung), Sound
  chip and Stereo (both with the chime, on A) while it is open, else
  SET-UP's; cancel restores the saved.
- Bell (bd 1ls.7): term.c's personalities bump term_t.bells on BEL (not
  OSC's terminator); main.c's loop calls bell_ring when it changes.
  `src/platform/audio.c` (the audio engine, bd 1ls.18.2; bell.h's API and
  audio.h's): a PIO program built at runtime (pio_encode_*,
  no pioasm: 9 instructions) shifting 16-bit stereo words as Philips I2S,
  which the TDA1387 takes (bd 1ls.18.1, as frank-386's audio_i2s.pio):
  left in the high half with WS low, WS switching one BCK before each MSB
  (it once switched with the MSB: the DAC read bit 14 as the sign, the
  bell twice as loud, PCM past half scale would wrap),
  `pull noblock` taking X = 0 when the FIFO is empty so the DAC's clocks
  never stop; 32 kHz from clk_sys (65 PIO cycles a frame); two DMA
  channels chained to each other over two 256-word blocks (8 ms), each
  block's end raising DMA_IRQ_1 on core 0 (audio_irq, SRAM: points that
  channel back at its block, src/sound/mixer.c refills it while the other
  plays; no channels: silence only, bell_present false). Each channel reads
  its 1 KB block as a ring (aligned), so with interrupts off it replays the
  block instead of running on through SRAM (that was a burst of noise on
  every settings save); nvflash's writes and fwupdate's commit call
  audio_silence (zeroes both, SRAM) first. check_sram_only.py follows
  audio_irq (OPTIONAL_ROOTS: boards without a DAC have none). mixer.c: the bell
  (mixer_ring: 3200 frames, 100 ms, of a 1 kHz square; a BEL while it runs
  is dropped) plus the AY when attached (audio_attach_ay, CP/M mode: Q15
  gain, skipped while ay_idle), clipped. Levels from the Bell setting
  (bell_set_level from apply_settings: 2 dB a level, 3 = 0x0800, 1/16 of
  full scale, headphone-safe; 10 = 0x2818; 0 silent): the bell's amplitude.
  The AY's gain from Sound chip (audio_set_ay_level, ay_gains: Q15, 2 dB a
  level, 10 unity, three channels at 15 just full scale; default 7). Interrupts off for a flash
  erase repeat the last blocks (a buzz) meanwhile. SET-UP sounds the edited Bell when it changes
  (setup_service) and restores the saved one on cancel. PCM output beyond
  that: bd 1ls.18.
- Sound (bd 1ls.18): `src/sound/` (portable dvt_sound, test_ay; in SRAM:
  its render runs in the audio engine's refill IRQ, 1ls.18.2).
  mixer.[ch]: the engine's mix (the bell, the AY), above.
  tune.[ch]: pieces (TUNE_*) of notes and steps: Korobeiniki (public
  domain; 64 eighths at 200 ms, looped) for SET-UP's T demo, melody on A
  with envelope shape 0 restarted per note, octave bass on B, a 40 ms noise
  tick on C each beat; the chime (C5 E5 G5 C6, 80 ms steps, once, ~0.7 s)
  for a new Sound chip level in the Clock and sound panel; tune_poll false
  when a piece ends. setup_glue.c's ay_play/ay_service/ay_give_back borrow
  the AY for them (bell_present; the demo only with Sound chip > 0; the Z80
  is stopped under SET-UP): its 16 registers and AY_STEREO_REG saved (that
  set to 0, so the edited Stereo is heard) and written back after (piece
  over, demo over, SET-UP closed), attached only meanwhile in terminal
  mode. test_ay's test_tune.
  ay8910.[ch] (1ls.18.3): the AY-3-8910, clock AY_CLOCK_RC2014 1.8432 MHz;
  generators ticked at f / 8 (tone f / (16 TP), noise and envelope steps
  f / (16 NP or EP), period 0 = 1, 17-bit LFSR taps 0/3, MAME's envelope
  logic), each output frame the average of its ticks (no aliasing; mixer
  off = the held level, sampled-sound trick), Ayumi's measured 16-level
  DAC (non-const: no flash from the IRQ), mono / ABC / ACB (ay8910_t.stereo,
  SET-UP's; AY_STEREO_REG 16, ours, bd 1ls.18.9: bits 0-1 written, 0 =
  .stereo, 1-3 = AY_STEREO_* + 1; read back with the layout in use + 1 in
  bits 4-5; ay_reset clears it; ay_stereo() the effective one; 17-255
  ignored, read 0xFF), a ~25 Hz DC blocker (dc_block, off in tests).
  audio.c's audio_irq adds its time to busy_us (timerawl, SRAM):
  audio_busy_us for the debug status line's "snd A%" (status.c, us a ms
  = per mille; the line's separators and units are tight to fit 80
  columns). ay_write only stores the masked byte
  (a shape write bumps env_writes; ay_render restarts the envelope), so
  the Z80 writing on core 0 under the IRQ is safe; never two cores.
- `src/platform/serial.[ch]`: the terminal UART's RX ring (IRQ) + TX
  queue, and the Flow control setting at runtime (serial_set_flow, bd
  dmt.27/68t.1): RTS/CTS = uart_set_hw_flow with the board's pins on
  GPIO_FUNC_UART (GPIO_FUNC_NULL otherwise: unwired as at power-on; CTS
  stays off under VIDEO_DIAG, whose blocking writes a held CTS would hang);
  XON/XOFF = `src/common/xonxoff.[ch]` (portable, test_xonxoff): the IRQ
  drops ^Q/^S from the data (they stop/restart serial_poll's queue) and
  asks for XOFF at half the 2 KB ring (again every 256 bytes more), XON at
  256; the control byte goes ahead of the queue, written by the IRQ or by
  serial_poll/serial_read with interrupts off (serial_poll's whole FIFO
  fill is interrupts-off, so the two never collide); leaving XON/XOFF
  while held sends XON. `src/ui/demo.[ch]`: opt-in startup demo. `src/platform/diag.[ch]`: VIDEO_DIAG.
- `src/render/logo_gen.py` + `src/render/logo.cmake`: PNG -> run-length encoded 2-bit
  levels (one hue; still <=2 levels per 8-px group, no longer needed).
  `src/render/splash.[ch]` (portable): `splash_check`, and `splash_compose`
  decodes the runs a scanline at a time (in order from the frame start; runs
  cross row ends) through 4 level colors (`splash_tint` +
  `splash_tint_color`). `video_splash()` copies the data into the text
  slots' raster memory (core 1 must not read flash) and core 1 shows it for
  scanlines 0-575 (status line from its slot); power-on/reset only, never
  on RIS.
- `src/render/font_gen.py` + `src/render/fonts.cmake`: 256-glyph tables (Terminus
  10x20 (16-bit rows) and its bold face, misc-fixed 6x12 shown doubled)
  generated at build time, glyph-major; glyph codes in `src/screen/font.h`. All
  in flash (FONT_FLASH_DATA, `.flashdata.font`), read through XIP by core
  0 only, when raster_select_font copies them; without a bold face (132
  cols) bold is only the brighter color (a smear closed the gaps of m, w,
  #: qur.9).
- Z80 CP/M machine (Mode setting "CP/M machine", bd lpt): the screen and
  keyboard become the console of a z80pack picosim-compatible machine on
  core 0 (`apps/terminal/cpm_glue.c` cpm_*: 10 ms CPU slices between keyboard, disks, screen).
  `src/machine/machine.[ch]` (portable): z80pack's Z80 core (`lib/z80pack`,
  vendored, MIT: UPSTREAM.md has the commit and our local changes; only
  z80core + iodevices/sd-fdc.c, built as dvt_z80pack by `src/machine/CMakeLists.txt`) configured
  by `src/machine/z80pack/{sim,simmem,simio,simport}.h`; picosim's port map (SIO1
  0/1 console, SIO2 2/3 = UART0, FDC 4, MMU 64, clock 65/66 without
  localtime from the platform's time_get (NULL: 1.1.2026 + uptime) and,
  ours, writable through time_set (registers 0-2 a field, 3 latched for
  4's day number; machine.h; CP/M 3's BIOS TIME C=0FFH writes them, so
  DATE SET works), 60 Hz timer 67, hwctl 160; ours: 68/69 a free-running
  millisecond tick, IN 68 latching 69's high byte, for timing without
  interrupts; the AY-3-8910 on RC2014's D8h select / D0h data, through the
  platform's sound_write/sound_read, NULL: 0xFF, reset writes 0s to
  registers 0-16 (MACHINE_AY_STEREO 16 back to SET-UP's), bd 1ls.18.4/.9;
  not MSX's A0h-A2h, A0h is hwctl) and boot ROM; bank 0 64 KB +
  MACHINE_BANKS x 48 KB in SRAM, or machine_set_banks's memory (before
  machine_init; NULL: back to the SRAM ones) of up to MACHINE_MAX_BANKS
  (15: IN 64 reports the count in its high nibble) x MACHINE_BANK_SIZE;
  machine_banks()/machine_bank(n) for the tests. Slices end from
  `machine_clock_us` (z80pack reads the clock every 20000 T-states and
  around port I/O); HALT waits call the platform idle hook and, at a slice
  end, stop with PC back on the HALT. z80pack's CPU state is globals (A, B,
  PC, T, ...): only machine.c includes its headers. `src/disks/drives.[ch]`
  (portable): 8 drives A-H; .DSK images in /DISKS80 via FatFs, format by
  size (8" FD 77x26, z80pack 4 MB HD 255x128, 8 MB HD 256x255; 128-byte
  sectors), kept open, flushed 250 ms after writes, DRIVES.CFG, blank image
  creation. Port 8 (machine.c, ours) tells the BIOS a drive's format
  (OUT drive, IN format, IN again drives_mounts: images mounted on it).
  Port 10 (ours, bd qur.2; protocol in machine.h): the file transfer
  device, FDC-style (SETDMA + commands moving 11-byte FCB names, 15-byte
  listing entries or 128-byte records by DMA), through the platform's
  xfer hook to `src/disks/xfer.[ch]` (portable, FatFs): /XFER per volume (0 SD,
  1 USB later), one FIL + one DIR, names CP/M can take only; RESET on
  machine_reset, the monitor's card re-check and the firmware commit.
  xfer.c is built -Os and xfer_out is cold (all code is SRAM: ~2.3 KB).
  CP/M side (bd qur.26, qur.28): src/machine/cpm/xls.asm, xpip.asm, xera.asm
  (erase; collects names, asks Y/N on wildcards) + xfer.inc (shared
  parser for d:/SD:/USB: specs, port helpers, 32-bit decimal), assembled
  by build.sh from src/machine/cpm (INCLUDE is cwd-relative) and put on CPM22 by
  diskimg_gen.py --file; --delete CPM22 XDIR.COM removes z80pack's XDIR.
  test_machine's test_xfer_tools runs them under CP/M.
  src/machine/cpm/termtest.asm (bd dmt.38) -> TERMTEST.COM on Z80TESTS (--file
  Z80TESTS): a page each for addressing, moves, erasing, IL/DL, scrolling,
  wrap, line graphics, replies and key codes, per personality (VT100,
  VT52 via CSI ? 2 l / ESC <, ADM-3A) from one table of sequences (OPS);
  test_machine's test_termtest runs it with the console fed into a real
  term_t (machine_host's host_console_hook; replies typed back) and checks
  every page's screen for all three.
  src/machine/cpm/altscr.asm (bd dmt.15) -> ALTSCR.COM on Z80TESTS: the main
  screen in four styles with an X, then ?1049 / ?1047 / ?47 in turn (the
  alternate screen scrolled, back on a key; ?1049 writes <1049 at the
  restored cursor in its bold red) and a step asking for SET-UP over it;
  test_machine's test_altscr runs it on a term_t with main.c's stash size.
  CP/M 3 (bd f8w): picosim's banked CP/M 3 disks are built in as CPM31
  (boots), CPM32 (picosim's cpm3-2.dsk files on an 8 MB disk:
  diskimg_gen.py --convert HD8; our WORMS/XLS/XPIP/XERA/MEM added) and
  CPM3GEN, and F83 (bd xdu): Laxen and Perry's F83 Forth for CP/M-80,
  lib/f83 (F83V2-80.ARK's files, UPSTREAM.md) on a blank 8 MB disk
  (test_f83). Its F83.COM is lib/f83's with src/machine/cpm/f83/
  viewdrv.fth (-> VIEWDRV.BLK, also on the disk) loaded and SAVE-SYSTEMed
  by f83/build.sh under CP/M 2.2 in cpmrun (cpm3/build.sh's, an output
  of that step): at boot VIEW-DRIVE puts 0050H (CP/M 3's load drive,
  only if BDOS 12 >= 30H and 1-16) into each VIEW-FILES FCB's drive
  byte, so VIEW works with F83 found through CP/M 3's search path.
  AY.BLK (bd 1ls.18.7, src/machine/cpm/f83/ay.fth, on the disk only, not
  loaded): AY! AY@ AY? TONE VOLUME NOISE VOICES ENVELOPE QUIET STEREO,
  HZ>PERIOD NOTE>PERIOD NOTE, TICKS WAIT, examples SCALE CHORDS SIREN DRUM
  PAN TUNE (PLAY a note/eighths table); 7 VIEWS AY.BLK. f83/build.sh's
  .fth converter: "---" between screens, "===" starts the shadow half
  (screen n's shadow at n + half; both halves padded alike: AY.BLK 7 + 7,
  shadows at n + 7), code lines <= 63 columns (a screen has no line ends:
  column 64 ran CHORD's ";" into the next ":" as ";:"), \ lines 64.
  test_f83 loads it, AY?, the periods, STEREO read back, VIEW, a shadow,
  SCALE's writes (fine before coarse, as ay_periods pairs them). Our BIOS's cold boot sets SETDEF's defaults in the SCB (search
  default, A:, B:; order COM,SUB; temporary drive B:; no display: f8w.9),
  so PROFILE.SUB (src/machine/cpm/cpm3/profile.sub) only loads VT100DYN, whose
  help text build.sh patches away; README.TXT (src/machine/cpm/cpm3/readme.txt)
  on CPM31 tells how to change them;
  booted by mounting them on A:/B:, no OS setting. CPM31's CPM3.SYS is ours
  (f8w.3/.4, src/machine/cpm/README.md): src/machine/cpm/cpm3/bnkbios3.asm (picosim's,
  8080/RMAC, drives A-H of any format via port 8; DPBs in common memory,
  DPHs banked, GENCPM sized by DPBMAX; READ/WRITE return 0FFH media
  changed when port 8's format or mount count differs from the log-in's;
  HD CKS 0, not 8000H: the BDOS ignores media changes on permanent
  drives) made by build.sh at build time: cpmrun.c (machine.c on the host,
  plain 8" images, a script at the console: sysgen.run) runs DRI's RMAC,
  LINK and GENCPM under CP/M 3; the work disk ships as CPM3GEN;
  diskimg_gen.py --edit/--extract handles the images. Unchanged picosim
  sources give picosim's CPM3.SYS byte for byte. machine.c still refuses
  FDC I/O to a 4/8 MB image on a drive port 8 was not asked about since
  the reset (FDC_STAT_NODISK; f8w.7), for BIOSes that never ask (MP/M's,
  picosim's), so they cannot read an HD as a floppy and write over it.
  src/machine/cpm/mem.asm (f8w.8) -> MEM.COM on CPM22 and CPM32: version, TPA,
  banks from IN 64; MEM T (1ls.17) fills every free bank (not 0, not the
  TPA's; refused under MP/M) with (L ^ H ^ bank) ^ mask, then checks
  them, mask 0 then FFh, errors per bank; its TFILL/TCHECK run at C000H
  (.PHASE, copied there; refused if the BDOS/RSX is below C100H), no stack
  while switched, interrupts off over the passes and restored from IFF2
  (LD A,I). test_machine: test_boot_cpm3, test_cpm3_tools,
  test_cpm3_drives, test_hd_guard (raw Z80 bytes in the ROM page, as
  test_xfer_port), test_mem_banks (15 banks in a host buffer: MEM, MEM T,
  the last pass's bytes).
  Port 12 (ours, bd 1ls.9; protocol and item numbers in machine.h): the
  hardware information device. OUT selects an item, IN reads its bytes
  (0 after the end); machine.c answers item 0 ('H' 'W' version) and keeps
  the item in a 128-byte buffer, the platform's hwinfo hook fills the
  rest (NULL: port 12 reads 0xFF, as on older firmware or picosim).
  Items 1/2 firmware and board text; 16+ a record per piece of hardware
  at a fixed number (status yes/no/missing/planned, name, detail): planned
  hardware (now only the NeoPixel, 1ls.15) is listed as HWINFO_ST_PLANNED
  until its bead lands, and its driver should then report the real state
  there; 29-31 (card detect, button, battery) are retired (bd 1ls.22). `apps/terminal/hwinfo_glue.c` (cold, in
  flash via ld/) builds them from board.h and runtime state; 32 lists the
  mounted images (HWINFO cannot ask port 8: that would unlock hard disks
  for BIOSes that never ask, f8w.7). src/machine/cpm/hwinfo.asm (1ls.10)
  -> HWINFO.COM on CPM22, CPM31 and CPM32: reads items 16-255, wraps
  details at 79 columns, pages (-- More -- after 23 lines, the command
  line counted; Q/ESC/^C stop). test_machine: test_hwinfo_port (raw Z80 bytes;
  no hook -> 0xFF; record truncation), test_hwinfo_tool (CP/M 2.2, and
  without the device) and test_cpm3_tools (CP/M 3), against
  machine_host.c's records.
  src/machine/cpm/aytest.asm (bd 1ls.18.6) -> AYTEST.COM on CPM22, CPM31
  and CPM32: finds the AY on D8h/D0h (register 1 reads back 0FH, 2 5AH),
  a menu (scale on A, chords on A/B/C, noise sweep, the 16 envelope shapes
  over C4, volume ramp, all), durations from the 68/69 tick (says so
  without it), a key stops a part (SP back to ITEMSP, QUIET); periods for
  1.8432 MHz (115200 / f). machine_host.c has an ay8910_t on the ports and
  logs the writes (host_ay_log): test_ay_ports (raw Z80 bytes, the tick,
  no hooks -> 0xFF), test_aytest_tool (periods, timing, the stop, no
  chip), test_cpm3_tools runs it under CP/M 3. Its menu shows the stereo
  layout (register 16; none when it reads 0xFF) and S steps it (0-3, the
  start's put back on exit), item 6 plays A, B, C in turn with where each
  is heard, 7 all. AYTEST and AYPLAY print through BDOS 6, as they read
  keys: CP/M 2.2's BDOS 2 takes a key typed during output into its own
  buffer, which 6 never sees (a key during a line did not stop the part;
  test_aytest_tool failed under a loaded ctest).
  src/machine/cpm/ayplay.asm (bd 1ls.18.10) -> AYPLAY.COM on CPM22 and
  CPM32: AYPLAY [d:]NAME[.PSG] [MONO|ABC|ACB] [LOOP] (command-tail words;
  none: the usage and the .PSG files, BDOS 17/18); PSG ("PSG" 1Ah + 12
  bytes; register + value, FFh frame end, FEh n = n x 4 frames, FDh end)
  read a record at a time (any size), frames every 20 ms on the 68/69
  tick (more than 200 ms behind: resynced), m:ss shown, Q/ESC/^C stop
  (QUIET, the layout found put back); LOOP reopens the file.
  src/machine/cpm/tunes/psg_gen.py renders KOROBEIN, JACQUES (a 3-voice
  round, entries 2 bars apart) and ODE.PSG at build time from note lists
  (equal temperament for 1.8432 MHz, per-frame volume decay, a silent last
  frame per note, noise drums; only changed registers written: sparse,
  ~50 writes in 100 frames). test_ayplay_tool (TUNES_DIR = the built
  tunes): the listing, JACQUES ABC's writes in order and on time against
  the file, the stop, missing file, bad option, not PSG.
  src/machine/cpm/aytrack.asm (bd 1ls.18.11) -> AYTRACK.COM on CPM22 and
  CPM32: a tracker on the VT100 screen (ESC [ row;col H, 7m reverse; keys
  decoded from ESC [ A-D / 2~ 3~ by RDKEY, waiting a little after ESC);
  the song at a fixed SONG = 3000H (the program ends ~12B9H; check the
  .lis when it grows), saved raw as NAME.AYT (26 records: "AYT1", speed,
  order count, 64 orders, 8 x 18-byte instruments: 16 volumes, mode bit 0
  tone / 1 noise, noise period; 16 x 32 rows x 3 x (note 12-95 = octave x
  12 + semitone, 0 empty, FFh off; instrument)). One player (PINIT,
  ROWST, FRAME, ADVANC; per channel period, instrument, step, FFh
  silent) drives the chip from the main loop on the 68/69 tick
  (PLAYTK: Space the song from the selected order, Return the pattern,
  looping; '>' markers on the rows and orders) and, OUTMOD 1, ^E's PSG
  export (once through, PENDF), only changed registers either way;
  PREVIEW plays an entered note or an edited instrument for 0.5 s
  (blocking) when nothing plays. Tab: pattern / orders / instruments
  (FOCUS, DOKEY then PATKEY/ORDKEY/INSKEY). test_aytrack_tool on a real
  term_t (rows checked as drawn): notes, orders, instruments, playing,
  save, export, reload, AYPLAY of the export. Ideas left out of the
  first version: copy/paste of patterns, effects (arpeggio, slides),
  the envelope generator, more than 8 instruments. cpm_glue's sound hooks are
  audio.c's audio_ay_write/_read (no DAC: dropped / 0xFF, so AYTEST finds
  no chip); main.c attaches the AY in CP/M mode. HWINFO item 33
  (HWINFO_SOUND) names it, none without a DAC.
  USB sticks (bd qur.3): FatFs volume 1 ("1:", USB: to the tools; ffconf
  FF_VOLUMES 2). `src/storage/diskio.c` (RP2350) dispatches FatFs drives: 0
  sdcard_disk_*, 1 usbmsc_disk_*. `src/storage/usbmsc.[ch]`:
  TinyUSB MSC host (CFG_TUH_MSC, LUN 0 only, 512-byte sectors; no exFAT),
  blocking I/O by running tuh_task until the transfer's callback (3 s
  timeout; gives up at once if the stick is detached). TinyUSB callbacks
  only note attach/detach; usbmsc_poll (main loop) mounts/unmounts.
  `src/disks/usbvol.[ch]` (portable, test_usbvol): which stick is volume 1:
  the first mounted; others wait in order; when it goes, the volume is
  unmounted (xfer_volume_gone(1)) before the next takes over, so nothing
  open reaches another stick; unmountable sticks are skipped until
  replugged. Status line: USB / +n / ? (lit like the drive letters).
  test/diskio_ram.c has drives 0 and 1. ~4.4 KB of SRAM (msc_host.c,
  usbmsc.c, usbvol.c built -Os).
  `src/machine/cpm/`: our 62K CP/M 2.2 (bios.asm 8-drive BIOS picking the DPB per
  drive at SELDSK, boot.asm), assembled at build time by build.sh with
  z80pack's z80asm built by the host cc; CCP+BDOS = picosim's 64K cpm.bin
  relocated with ccpbdos.rel (from MOVCPM 62 *); diskimg_gen.py --system
  writes the system tracks of CPM22 (README there); worms.asm (Z80
  mnemonics) -> WORMS.COM, put on CPM22 by diskimg_gen.py --file (writes
  into the 8" CP/M directory); host tests in test_machine (machine_host
  answers ESC [ 6 n through host_cpr_reply). `src/ui/monitor.[ch]`
  (portable): menu (boot, mount, list, new blank disk, install, speed
  test, rescan), 3 s autoboot. `src/ui/drivepanel.[ch]` (portable): SET-UP's
  Drives panel (D in CP/M mode); setup_glue.c defers its FatFs work (mount,
  save/remove a set: drives_work) to the main loop (setup_feed runs inside
  the USB stack). Named drive sets (bd f8w.6): /DISKS80/SETS/NAME.SET in
  DRIVES.CFG's form (A=NAME lines, hand-editable), at most 16; the panel's
  Set row picks one (fills the choices; Return mounts), S saves the
  choices (name prompt), R/Delete removes (Y/N); the monitor knows only the
  current mounts (its install, I, writes the starter sets CPM22 and CPM3
  when missing). `src/disks/drivecfg.c` (cold, in flash): the image list,
  DRIVES.CFG and the sets, split from drives.c (sector I/O, SRAM). Status line: drive letters
  only, reverse while accessed. `src/disks/diskimg.[ch]` + `src/common/lz.[ch]` (portable)
  + `src/disks/diskimg_gen.py` (`src/disks/diskimg.cmake`): CPM22, Z80TESTS, EMPTY
  from z80pack as LZ4-format blocks (offsets < 4 KB, streamed through a
  4 KB window: `src/common/scratch.[ch]`'s buffer) in `.flashdata.diskimg`, written to the card by the
  monitor. `src/storage/sdcard.[ch]` (RP2350): polled SPI0 SD driver = FatFs
  diskio (GP4 MISO, GP5 CS, GP6 SCK, GP7 MOSI; 400 kHz init, 25 MHz).
  `lib/fatfs`: ChaN FatFs R0.15a vendored (ffconf: FIND, MKFS, CP437,
  timestamps on (FF_FS_NORTC 0: get_fattime in storage/diskio.c from
  rtc_now, test/diskio_ram.c a fixed 1.1.2026), TINY (files share the
  volume's sector buffer), no LFN; ffconf.h
  has CRLF line endings, edit it byte-wise). Host tests use `test/diskio_ram.c`. Changing Mode
  saves and reboots (watchdog). In SET-UP, M leaves for the monitor.
  SET-UP's T (both modes) shows the demo until a key (setup_glue.c
  setup_demo_*; status line draws skip it), then term_set_columns clears
  the screen (no 13 KB copy) and SET-UP is redrawn over it. F11
  is the machine's reset key in CP/M mode (kbd_set_reset_key; terminal
  mode keeps ESC [ 23 ~).
  Firmware update (bd n3k): SET-UP's U in either mode (`src/ui/fwpanel.[ch]`,
  portable, over SET-UP's rows; setup_glue.c fw_* platform: in terminal mode it
  f_mounts the card only while open, with the monitor's FATFS
  (monitor_card_fs, qur.15: the monitor never runs in terminal mode), in
  CP/M mode it uses the monitor's mount; stubbed in test_machine). It looks for FIRMWARE.UF2 on the card
  and on the USB stick (volume 1, usbmsc's mount, platform stick hook;
  qur.27): one is checked, both ask S or U; renamed on its own volume. The
  stick needs nothing at the commit (the rename synced it, xfer_reset
  closes its files). The monitor's U was removed (qur.10):
  SET-UP's is the only way.
  Card format (bd 1ls.19.7): the monitor's F (separate from I) asks for
  FORMAT typed out (line_key, MODE_FORMAT; Esc or anything else: "Not
  formatted."), then drives_flush, xfer_reset, drives_unmount_all and
  `src/disks/sdformat.[ch]` (portable, cold, -Os, in flash; test_sdformat
  on its own sparse disk, 1 MB to 119 GB volumes): sdformat_choose from
  GET_SECTOR_COUNT: < 2 GiB FM_FAT (FatFs picks FAT12/16 and the
  cluster), else FM_FAT32 with 4 KB clusters < 4 GiB, 32 KB to 64 GiB,
  64 KB above; data area aligned 128 / 8192 sectors (FatFs aligns from
  LBA 0; its MBR partition starts at 63), 2 FATs, no exFAT (LFN off).
  f_mkfs uses the scratch buffer (Busy. while held); then mount_card,
  can_resume off. The unreadable-card line ends ": F formats it". `src/settings/uf2.[ch]` (portable; test_uf2 also checks
  build/pico2-dvi-sock/dvi_terminal.uf2 when built): skips other families
  (the E10 block numbers its own sequence); ours in order, ascending
  addresses, PICOBIN marker in the first 4 KB; CRC of the image as flashed
  (gaps 0xff). `src/platform/fwupdate.[ch]` (RP2350): stage = check again while
  programming the staging area (flash offset 2 MB; images up to
  NVFLASH_OFFSET - 2 MB), verify by CRC; the file is read 4 KB at a time
  (FWUPDATE_READ_SIZE: multi-sector commands, a stick takes ms per
  command, bd akb), and the check's line says how long reading took; fwpanel then renames the file
  to FIRMWARE.OLD and unmounts; the commit (fwupdate_commit_image,
  interrupts off for good) copies sector by sector through the 4 KB
  scratch buffer (`src/common/scratch.[ch]`, portable: one holder at a time; also
  the monitor's LZ window and blank-disk chunks, and fwupdate's UF2 block), skipping equal sectors, then watchdog_reboot. It must read
  nothing from the old image (.rodata is in flash):
  `scripts/check_sram_only.py` (run by build.sh; was check_fwupdate.py)
  follows its calls in the .dis and fails on 0x10xxxxxx literals or code
  outside SRAM, or if the image reaches the staging area; the same for
  core1_main, dma_irq, video_init and main()'s calls up to
  nvflash_retime, and the nvflash operations (ADDRESS_ONLY lists flash
  addresses only compared against). nvflash_erase/program/write: raw
  access, interrupts off per operation (USB handlers' code is in flash).
  newlib's localtime/scanf cost ~20 KB of SRAM, so the clock avoids them.
- `tools/hostbridge.py`: run a host program with the terminal as display,
  typing on the PC.
- CI (`.gitlab-ci.yml`): test -> build (matrix of targets) -> package ->
  release (tags `vX.Y.Z`). Pipeline status is not visible from here.

## Resource budget (v0.6.1, MACHINE_BANKS=2)

SRAM is the tight resource; peripherals and pins are mostly free.

- SRAM (520 KB): .text 170 KB, .data 20 KB (Z80 tables, video code
  copied to RAM), .bss 285 KB (3of figures); `__end__` 0x20077f4c, so ~32 KB free below
  0x20080000 (no malloc user); pb2's palette (tables + code) moved it to
  0x200790d8, ~28 KB free; the qur QoL branch (taller SET-UP, fwpanel's
  FATFS, code) to 0x2007b570, ~18.6 KB free; qur.8's packed raster rows
  (fonts 10x20 and the bold faces in flash) back to 0x2007aa00, ~22 KB
  free; qur.20's SRAM font pair (20 KB, 8x16 and the FONT_132 variants
  dropped) to 0x2007dac4, ~9.3 KB free; qur.23 (no printf float, FatFs
  tiny, 2 KB UART RX ring) to 0x2007af8c, ~20.1 KB free; 3of (one 4 KB
  scratch buffer for the LZ window and fwupdate) to 0x20076e58, ~37 KB
  free; file transfer (qur.2, ~2.3 KB) and USB sticks (qur.3, ~4.4 KB) to
  0x20078934, ~30.4 KB free; the USB debug log and video-fixes (ful,
  3rk) to 0x200796a0, ~26.3 KB free (v0.6.1: .text 177 KB, .data 21 KB,
  .bss 287 KB); firmware update from a stick (qur.27, ~1 KB) to
  0x20079a88, ~25.3 KB free; board headers (axq, pin strings) to
  0x20079ac8; terminal personalities (dmt.37 split, dmt.16 VT52, dmt.28
  ADM-3A, SET-UP's Terminal type row, status line type) and s0i to
  0x2007a2b8 (v0.7.0), ~23.8 KB free; dmt.36/39 to 0x2007a4d0; the
  alternate screen with SET-UP's rows in one 13.4 KB stash (qur.15/16,
  dmt.15: +4.4 KB) to 0x2007b640, ~18.4 KB free; cold code in flash
  (qur.24, -48.5 KB) to 0x2006f458, ~67 KB free; status LED, Feather
  board (1ls.8, 1ls.14.1) to 0x2006f6b0; the library restructure (gr1:
  libraries identical, main.c's split outlines what was inlined into
  main(), -648 bytes) to 0x2006f428; the clock (src/clock, rtc.c, 1ls.6.1)
  to 0x2006fda4 (Frankless with PSRAM banks: 0x20058d94); TinyUSB master
  (bd xv0: its bigger hcd_rp2040 ~5.9 KB, the USB log's 2 KB text and the
  watchdog) from 0x200709b0 to 0x20073288, ~52 KB free; the USB log setting's SET-UP row (stash) to 0x20073444; crash_glue.c, the stick workarounds to 0x2007401c (~48 KB free); the
sound (bd 1ls.18: dvt_sound, the settings and panel rows; no DAC on the
Sock, so no audio engine running) to 0x20074840, ~45.9 KB free; flow
control (dmt.27, xonxoff.c, the SET-UP row) and the RAM disk (1ls.20) to
0x200754b4, ~42 KB free (v0.9.5; Frankless 0x20060598, PSRAM Feather
0x2005dc98). Scratch X: core 1's stack is in .bss, so nearly all 4 KB free; scratch Y: 2 KB core 0
  stack, ~2 KB free. Biggest .bss: Z80 banks 96 KB + bank 0 64 KB, raster
  60 KB, font_copy 20 KB, screen 13 KB, stash_buf 14.1 KB, line_buf 6.4 KB,
  scratch 4 KB, UART RX ring 2 KB. A third Z80 bank (48 KB) does not fit
  (the PSRAM build has none in SRAM: __end__ 0x20057b88).
- Flash (4 MB): image 0-0x7e108 (504 KB at v0.6.1; the 4 KB LZ window costs 32 KB;
  ~875 KB with the CP/M 3 disks, f8w.1: ~610 KB of compressed images in all;
  1.05 MB with F83, xdu: ~810 KB of images; 1.12 MB with the sound, bd
  1ls.18: AYTEST, AYPLAY, its tunes, AY.BLK); free to 2 MB (image growth;
  fwupdate caps images at 2 MB - 12 KB); staging 0x200000-0x3fcfff (used
  only during an update, up to the new image's length); settings
  0x3fd000-0x3fefff; last sector left blank (E10). New persistent storage
  has to come out of the image's growth room or lower the image cap.
- Clocks: clk_sys 400 MHz at 1.3 V, clk_hstx 200 MHz, clk_usb 48 MHz. Core
  load not measured (video_stats().late shows core 1 falling behind).
- In use: HSTX; DMA ch x2 + DMA_IRQ_0 (core 1); UART0 + its IRQ; SPI0
  (polled); USB host + USBCTRL_IRQ; TIMER0 (time base, SDK alarm pool);
  watchdog (reboots only). Free: 14 DMA channels, DMA_IRQ_1-3, all of PIO0-2
  (12 SMs), UART1, SPI1, I2C0/1, PWM, ADC, TIMER1, TRNG, SHA-256, QMI CS1
  (PSRAM; GP8 can carry its select; spike bd g31).
- GPIO: GP0/1 UART0, GP2/3 CTS/RTS (in use only with Flow control
  RTS/CTS, but the board header claims them), GP4-7 SD, GP12-19 DVI Sock,
  GP25 LED (status, src/platform/led.c), GP23/24/29 board internal. Free:
  GP8-11, GP20-22, GP26-28 (ADC capable).

### Frankless (TARGET=frankless, PSRAM_BANKS, 2026-10-22: bd 1ls.3.1)

- GPIO: all of GP0-23 taken (GP0 PSRAM CS, GP1-4 SD on SPI0, GP5-7
  TDA1387 BCK/WS/DATA, GP8 RTC 1 Hz, GP9 LED, GP10/11 I2C1 RTC, GP12-19
  HSTX, GP20-23 UART1 on J8 incl. CTS/RTS). Free: GP24-29 on header J1
  only: UART0 GP28 TX / GP29 RX (a second serial port, bd qtq), SPI1 GP24
  RX / 25 CS / 26 SCK / 27 TX, I2C0 GP24/25 or GP28/29, ADC0-3 GP26-29,
  PWM slices 4-6, PIO.
- PIO: one SM and 9 of 32 instructions (audio.c, block picked at runtime,
  normally PIO0); 11 SMs free. DMA: 4 of 16 channels (video 2, audio 2),
  DMA_IRQ_0 core 1's, DMA_IRQ_1 core 0's (audio); IRQ_2-3 free. IO_IRQ_BANK0 has the RTC tick's raw
  handler (shared: add more). UART0, SPI1, I2C0, TIMER1, PWM, ADC, TRNG,
  SHA-256, POWMAN's always-on timer free; I2C1 can take more chips (other
  addresses). No spare PLL (PLL_SYS 400 MHz, PLL_USB 48 MHz; bd cpx.1).
- SRAM: __end__ 0x2005d78c (bd xv0); 0x2005f428 with the audio engine and
  the AY (bd 1ls.18, +7.2 KB), 0x2005f800 with stereo and the busy count
  (1ls.18.9/.8), ~130 KB free (the Z80 banks are in the
  PSRAM). PSRAM 8 MB: 720 KB banks (15 x 48 KB, port 64's limit), the RAM
  disk 4080 KB (HD4, bd 1ls.20), ~3.4 MB free (scrollback, 1ls.21, deferred). Flash 16 MB: image 1.04 MB, growth
  room to 8 MB (FWUPDATE_STAGING = half the flash), settings in the top
  12 KB.
- Core 1: video only (62% of a line at 80 columns, 87-92% at 132, qur.22's
  Sock figures). Core 0: main loop + the Z80 unthrottled, so new core 0
  work costs Z80 speed; the audio engine's refill IRQ (mixer, AY render)
  is the debug status line's "snd A%": 6-7% on the Frankless, playing or
  idle (the AY is rendered every refill, silent or not; bd 1ls.18.12).

## Conventions & Patterns

- Style: C11, tabs, K&R braces, comments explain why; match surrounding
  code. Portable libraries must not include pico-sdk headers (host-tested;
  the host build fails if one does). A new source goes into its
  library's CMakeLists.txt; a new library into src/CMakeLists.txt (RP2350
  ones under DVT_FIRMWARE) and the programs' target_link_libraries.
- Hardware constraints learned the hard way:
  - Code runs from SRAM (`COPY_TO_RAM`): the boot XIP setup assumes
    clk_sys <= 150 MHz (on the RP2040, flash execution hung above ~252 MHz:
    dmt.10). Cold code is the exception (bd qur.24): ld/
    section_copy_to_ram_rodata.incl (pico_add_linker_script_override_path)
    keeps whole objects' .text and .rodata in flash, run through XIP at
    03h/50 MHz after nvflash_retime: FatFs, TinyUSB's usbh/hub/hid_host/
    msc_host, usblog_glue, monitor, drivepanel, drivecfg, setup, fwpanel, demo, palette, xfer,
    usbmsc, lz, diskimg, keyname, settings, uf2, qsort, machine.c's
    .rodata (~51 KB). Hot paths (Z80 core, machine.c code, term, vtparse,
    raster, video, screen, sdcard, drives, serial, keyboard, hcd_rp2040,
    printf for panic) stay in SRAM. Nothing may run from flash on core 1,
    between video_init and nvflash_retime, while flash is written, or in
    the update's commit: check_sram_only.py. Adding an object there: check
    it is none of those. Objects in our libraries are named
    *libdvt_<name>.a:<file>.c.o (or *libdvt_<name>.a:* for a whole
    library: ui); a pattern that places nothing (renamed, moved) would
    leave its code in SRAM silently, so check_sram_only.py fails the
    build on one.
  - Core 1 must never touch flash or `const` data (which lands in flash):
    keep its data non-const/static and its code `__not_in_flash_func`.
  - The HSTX DMA reads line buffers in SRAM at high bus priority; a line
    not composed in time shows stale pixels (video_stats().late), it does
    not break sync.
  - The 2 KB core 0 stack is small: big scratch arrays are static.
  - Heap = SRAM left after .bss (code is in SRAM too); malloc failure
    panics silently (nothing links malloc today). Check `__end__` in the
    .elf.map when adding big static data or libraries: see "Resource
    budget".
  - Pin functions for CTS/RTS use `GPIO_FUNC_UART` (UART_FUNCSEL_NUM is
    wrong for GPIO 2/3 on RP2350).
  - TinyUSB 0.18 had one control transfer for the whole host: one started
    from a TinyUSB callback (e.g. the keyboard LEDs' SET_REPORT) made a
    device still being set up lose its next request, stay half set up,
    and TinyUSB "enumerate" it for good, so nothing plugged in later (a
    stick, the keyboard moved to another hub port) was ever set up (qur.3).
    The pinned master queues control transfers, but usbkbd.c still sends
    the LEDs (and the watchdog's probe) from usbkbd_poll only once every
    device has had tuh_mount_cb (hubs: tuh_mounted). The monitor's USB:
    line (usbmsc_info) shows each device, "setting up", HID n/m and the
    host restarts.
  - RP2350 USB host: never start an EPX transfer (bulk or control) while
    the controller may be polling interrupt endpoints by itself; pause
    them first (usbkbd_polls_pause, bd xv0). Debugging the USB host:
    SET-UP's USB log setting (USBLOG.TXT; TinyUSB's TU_LOG1 lands there
    too, so temporary TU_LOG1 lines in lib/tinyusb are the quickest probe)
    and crash_glue.c's status-line reports.
- Git: work on a branch, merge to `main` with `git merge --no-ff`, push only
  when the user asks (they usually say "commit and push" / "merge").
  Commit messages end with the Co-Authored-By line. Tags `vX.Y.Z` trigger a
  GitLab release.
- bd: the pre-commit hook re-exports `.beads/issues.jsonl` after `bd close`
  in the same command; check `git status` after committing and amend if
  needed before switching branches. Chain git steps with `&&`, not `;`.
- When writing C sources from a Python script, keep escapes such as `\r`,
  `\n` and `\x1b` escaped (raw strings or a quoted shell heredoc);
  otherwise they turn into raw control bytes in the file. Check with
  `grep -lP '[\x00-\x08\x0b-\x1f]' src/* test/*`.
- User preferences: flow control off by default (Flow control none); 80 columns at the full
  width (10x20) only, the centered 8x16 layout dropped (qur.20); smooth
  scrolling, double-size lines and RP2350 HSTX are deferred nice-to-haves.
