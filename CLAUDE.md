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
git submodule update --init          # lib/pico-sdk (2.3.1), not its submodules (lib/z80pack is vendored)
git -C lib/pico-sdk submodule update --init --depth 1 lib/tinyusb   # only nested one used
scripts/setup-toolchain.sh           # pinned CMake, Arm GNU Toolchain, picotool (toolchain.env)
scripts/test.sh                      # host unit tests (gcc + ASan/UBSan): screen, vtparse, term, raster, keyboard, keyname, settings, setup, splash, uf2, lz, machine (boots CP/M 2.2 from a FAT32 RAM volume)
CPU_TESTS=1 scripts/test.sh          # also the Z80 exercisers (test_cpu, ~3 min, optimised, no sanitisers): EXZ80DOC, EXZ80ALL and CPUTEST under CP/M; build/test/test_cpu <PROGRAM> runs any program on B:
scripts/build.sh                     # TARGET=pico2-dvi-sock (default, the only one) -> build/<target>/dvi_terminal.uf2
scripts/package.sh                   # every built target -> dist/dvi-terminal-<describe>.zip
```

Build options (CMake; `scripts/build.sh` passes `SERIAL_FLOW_CONTROL` from
the environment, other options via `CMAKE_ARGS`):
`SERIAL_FLOW_CONTROL` (RTS/CTS on GP3/GP2, default OFF), `STARTUP_LOGO`
(default ON; image from `LOGO_IMAGE`, shown `LOGO_MS`, default 10000: monitors
need seconds to sync), `LOGO_IN_FLASH` (default ON, experimental dmt.25: logo data in a
`.flashdata` section read via XIP after `nvflash_retime()`; frees ~19 KB of
SRAM), `STARTUP_DEMO` (default OFF),
`VIDEO_DIAG` (UART0 TX diagnostics, default OFF; also prints
panic() text), `USB_KEYBOARD` (TinyUSB host keyboard; default ON),
`COPY_TO_RAM` (default ON; must stay ON, see below), `FONT_132` (6x12 at
double height, default, fills the 24-line rows; 6x10 doubled; 6x13 single;
cmake/fonts.cmake's FONT_DEFINITIONS give raster.h FONT_132_HEIGHT/SCALE;
test_raster_<variant> covers the others; a build dir keeps its cached value),
`MACHINE_BANKS` (Z80
machine: 48 KB banks besides bank 0, default 2; each costs 48 KB of SRAM).

Always run `scripts/test.sh` and a firmware build before handing work
back. Hardware checks are done by the user (no board is attached to the
dev host; Docker is not usable for CI simulation).

## Architecture Overview

VT100 serial terminal with DVI output for a Pico 2 (RP2350) with the Pico
DVI Sock. RP2040 (Feather RP2040 DVI) support was dropped (dmt.29): out of
SRAM, and the USB keyboard crashed it (dmt.20); CMake rejects non-RP2350
boards. 800x600p60 (400 MHz, 1.3 V); 80 or 132 columns x 24 rows plus a
status line (row 24); UART0 on GP0/GP1.

- `src/screen.[ch]` (portable): cell buffer: glyph code, fg/bg palette
  indices and attribute bits per cell; `row_map` (display row -> line slot) so
  scrolling rotates indices; `dirty` bit per slot; `reverse_rows` mask.
- `src/vtparse.[ch]` (portable): Paul Williams' DEC parser. C1 (0x80-0x9F)
  ignored unless `c1_controls` (keeps UTF-8 safe); OSC/DCS/APC discarded.
- `src/term.[ch]` (portable): VT100/VT102 emulation on the screen; replies
  (DA, DSR/CPR, DECREQTPARM) through a callback. Writes palette indices
  only (nothing depends on the palette): default fg 7, bold default 15, bg
  0; bold brightens 0-7 to 8-15; 38;5;n as is; 24-bit -> palette_nearest.
- `src/palette.[ch]` (portable, bd pb2): the xterm-256 palette as RGB565
  from Colors (hue) x Palette (mono): white/colour = our ANSI set + xterm
  16-255; phosphor/colour = 0-15 a ramp in the hue by bands (7 at 2/3, 15
  full) + xterm 16-255; mono = all 256 as a ramp by luma (floor 35%).
  main.c show_palette() -> video_set_palette (two 256-word tables, core 1
  switches at frame start). A palette change re-renders nothing. While
  SET-UP is open the edited palette is shown (restored on cancel).
- `src/raster.[ch]` (portable): three layouts (raster_layout_t: 80 cols
  8x16 centred, 80 cols 10x20 at full width = the "80 columns" setting via
  video_set_wide80, 132 cols). `raster_render_line` (core 0) turns a
  screen line into, per stored row, a bit string of the cells' pixels
  (cell c from bit c * width, 100 bytes per row for every layout;
  raster_cell_bits reads one) plus per-cell fg/bg (palette indices, reverse
  applied). Stored rows per line = font rows + 2 padding rows; connectors
  (line drawing, blocks, shades) extend into the padding. Handles
  underline, blink phase, bold (bold face, or a smear in 132 cols),
  reverse. `raster_compose` (core 1) makes a scanline of RGB565 pixel pairs
  from a stored row through the 256-entry palette table, inverting the
  cursor cell; every cell is exact in every layout. 132 cols centred at
  x = 4. Core 1's helpers are ternaries, no tables (GCC's CSWTCH tables
  land in RAM, but check the .dis when changing them).
- `src/hstx_dvi.[ch]` (RP2350): DVI from the HSTX on the DVI Sock pins
  (GP12-19 = HSTX bits 0-7: D0 0/1, CK 2/3, D2 4/5, D1 6/7). clk_sys = 10 x
  pixel clock, clk_hstx = clk_sys / 2 (set explicitly: the SDK leaves it
  undivided from clk_sys); RGB565 through the TMDS expander; two chained
  DMA channels, reloaded by DMA_IRQ_0 with blanking command lists and
  per-line pixels from a callback. `HSTX_MODE` 0 = 800x600@60 with libdvi's
  timing (clk_hstx 200 MHz, above the 150 MHz rating: dmt.8.1), 1 = @56,
  2 = CVT-RB. `src/hstx_test.c` -> `hstx_test.uf2`: test pattern.
- `src/video.[ch]` (RP2350): core 1 composes scanlines (raster_compose or
  splash_compose) into a ring of 4 line buffers, at most 2 lines ahead of
  the DMA interrupt (also on core 1), latching mode/cursor/splash per
  frame; counts late lines. `video_update()` on core 0 rasterises dirty
  slots and the blink phase.
- `src/keyboard.[ch]` (portable): HID boot keyboard reports -> VT100 key
  codes (DECCKM, DECKPAM, LNM, Num/Caps Lock, autorepeat via kbd_tick).
- `src/usbkbd.[ch]` + `src/tusb_config.h` (USB_KEYBOARD builds, the
  default): TinyUSB host on the native USB port, boot keyboards only; keys
  go to the serial TX queue. The board does not power VBUS in host mode
  (5 V fed into the VBUS pin).
- `src/settings.[ch]` (portable): settings record (magic, seq, CRC-32,
  one byte per setting, append-only) in two flash sectors used in turn.
  Settings (settings_fields[] drives the record, validation and SET-UP
  labels; append only): columns, backspace (DECBKM RIS value), local_echo,
  baud, framing, newline (LNM RIS value), colours (hue), palette (mono),
  mode (terminal / CP/M machine), status_line (hidden: main.c
  status_shown() leaves row 24 blank), blank_after (minutes; `src/blank.[ch]`
  portable idle timer, keys only, the waking key dropped; video_set_blank
  makes core 1 send black lines); settings_colours() gives the status
  line's indices and the logo's tint. CRC-32 in
  `src/crc32.[ch]` (shared with the disk images). main.c apply_settings()
  -> serial_configure, term_set_defaults, show_palette.
  `src/setup.[ch]` (portable): SET-UP panel over the bottom text rows,
  in a box (screen_frame: DEC line glyphs in the status bg colour, text
  in the default colours; also the Drives panel) (one row per setting, plus two preview rows under Palette: ANSI colours
  as fg and as bg, spaced so no 8-px group held two samples in 132 cols
  under the old PicoDVI encoder)
  (key bytes in, save/cancel out); main.c saves/restores those rows. Opened
  by F12 or Pause (keyboard.c SET-UP callback) or host `CSI ? 999 ~` (term.c bumps
  `setup_requests` and term_write stops after it; later bytes are keys).
- `src/keyname.[ch]` (portable): key bytes -> "ESC [A", "^C", ... for the
  status line's last-key field (which replaced the rx/tx/err counters).
- `src/nvflash.[ch]`: the 2 flash sectors before the last (UF2 files' E10
  workaround block at 0x10ffff00 wraps to the last sector and erases it). Read at boot before
  video_init; `nvflash_retime()` after the clock change. SDK
  flash_range_* (ROM uses QMI clkdiv 12) + `rom_flash_select_xip_read_mode`
  03h at <=50 MHz afterwards (the boot XIP stub the SDK replays assumes
  <=150 MHz). No flash_safe_execute (core 1 in RAM).
- `src/serial.[ch]`: UART0 RX ring (IRQ) + TX queue, optional hardware
  RTS/CTS. `src/demo.[ch]`: opt-in startup demo. `src/diag.[ch]`: VIDEO_DIAG.
- `src/logo_gen.py` + `cmake/logo.cmake`: PNG -> run-length encoded 2-bit
  levels (one hue; still <=2 levels per 8-px group, no longer needed).
  `src/splash.[ch]` (portable): `splash_check`, and `splash_compose`
  decodes the runs a scanline at a time (in order from the frame start; runs
  cross row ends) through 4 level colours (`splash_tint` +
  `splash_tint_colour`). `video_splash()` copies the data into the text
  slots' raster memory (core 1 must not read flash) and core 1 shows it for
  scanlines 0-575 (status line from its slot); power-on/reset only, never
  on RIS.
- `src/font_gen.py` + `cmake/fonts.cmake`: 256-glyph tables (Terminus 8x16
  and 10x20 (16-bit rows) with their bold faces, misc-fixed 6x12/6x10
  doubled or 6x13 by FONT_132: raster_font_scale) generated at build time,
  glyph-major; glyph codes in `src/font.h`. Tables built with FLASH
  (font_8x16_bold, font_10x20, font_10x20_bold) stay in flash
  (FONT_FLASH_DATA, `.flashdata.font`) and are read through XIP by core 0
  only (raster_render_line); without a bold face (132 cols) bold is a
  1-px smear that never fills the cell's last column.
- Z80 CP/M machine (Mode setting "CP/M machine", bd lpt): the screen and
  keyboard become the console of a z80pack picosim-compatible machine on
  core 0 (`main.c` cpm_*: 10 ms CPU slices between keyboard, disks, screen).
  `src/machine.[ch]` (portable): z80pack's Z80 core (`lib/z80pack`,
  vendored, MIT: UPSTREAM.md has the commit and our local changes; only
  z80core + iodevices/sd-fdc.c, built by `cmake/z80pack.cmake`) configured
  by `src/z80pack/{sim,simmem,simio,simport}.h`; picosim's port map (SIO1
  0/1 console, SIO2 2/3 = UART0, FDC 4, MMU 64, clock 65/66 without
  localtime, 60 Hz timer 67, hwctl 160) and boot ROM; bank 0 64 KB +
  MACHINE_BANKS x 48 KB. Slices end from
  `machine_clock_us` (z80pack reads the clock every 20000 T-states and
  around port I/O); HALT waits call the platform idle hook and, at a slice
  end, stop with PC back on the HALT. z80pack's CPU state is globals (A, B,
  PC, T, ...): only machine.c includes its headers. `src/drives.[ch]`
  (portable): 8 drives A-H; .DSK images in /DISKS80 via FatFs, format by
  size (8" FD 77x26, z80pack 4 MB HD 255x128, 8 MB HD 256x255; 128-byte
  sectors), kept open, flushed 250 ms after writes, DRIVES.CFG, blank image
  creation. Port 8 (machine.c, ours) tells the BIOS a drive's format.
  `src/cpm/`: our 62K CP/M 2.2 (bios.asm 8-drive BIOS picking the DPB per
  drive at SELDSK, boot.asm), assembled at build time by build.sh with
  z80pack's z80asm built by the host cc; CCP+BDOS = picosim's 64K cpm.bin
  relocated with ccpbdos.rel (from MOVCPM 62 *); diskimg_gen.py --system
  writes the system tracks of CPM22 (README there); worms.asm (Z80
  mnemonics) -> WORMS.COM, put on CPM22 by diskimg_gen.py --file (writes
  into the 8" CP/M directory); host tests in test_machine (machine_host
  answers ESC [ 6 n through host_cpr_reply). `src/monitor.[ch]`
  (portable): menu (boot, mount, list, new blank disk, install, speed
  test, rescan, firmware update), 3 s autoboot. `src/drivepanel.[ch]` (portable): SET-UP's
  Drives panel (D in CP/M mode); main.c defers its FatFs work to the main
  loop (setup_feed runs inside the USB stack). Status line: drive letters
  only, reverse while accessed. `src/diskimg.[ch]` + `src/lz.[ch]` (portable)
  + `src/diskimg_gen.py` (`cmake/diskimg.cmake`): CPM22, Z80TESTS, EMPTY
  from z80pack as LZ4-format blocks (offsets < 16 KB, streamed through a
  16 KB window) in `.flashdata.diskimg`, written to the card by the
  monitor. `src/sdcard.[ch]` (RP2350): polled SPI0 SD driver = FatFs
  diskio (GP4 MISO, GP5 CS, GP6 SCK, GP7 MOSI; 400 kHz init, 25 MHz).
  `lib/fatfs`: ChaN FatFs R0.15a vendored (ffconf: FIND, MKFS, CP437,
  NORTC, no LFN). Host tests use `test/diskio_ram.c`. Changing Mode
  saves and reboots (watchdog). In SET-UP, M leaves for the monitor.
  SET-UP's T (both modes) shows the demo until a key (main.c
  setup_demo_*; status line draws skip it), then term_set_columns clears
  the screen (no 13 KB copy) and SET-UP is redrawn over it. F11
  is the machine's reset key in CP/M mode (kbd_set_reset_key; terminal
  mode keeps ESC [ 23 ~).
  Firmware update (bd n3k): SET-UP's U in either mode (`src/fwpanel.[ch]`,
  portable, over SET-UP's rows; main.c fw_* platform: in terminal mode it
  f_mounts the card only while open, in CP/M mode it uses the monitor's
  mount), and the monitor's U, offered when /FIRMWARE.UF2
  is on the card (monitor_platform_t firmware_* hooks; stubbed in
  test_machine). `src/uf2.[ch]` (portable; test_uf2 also checks
  build/pico2-dvi-sock/dvi_terminal.uf2 when built): skips other families
  (the E10 block numbers its own sequence); ours in order, ascending
  addresses, PICOBIN marker in the first 4 KB; CRC of the image as flashed
  (gaps 0xff). `src/fwupdate.[ch]` (RP2350): stage = check again while
  programming the staging area (flash offset 2 MB; images up to
  NVFLASH_OFFSET - 2 MB), verify by CRC; the monitor then renames the file
  to FIRMWARE.OLD and unmounts; the commit (fwupdate_commit_image,
  interrupts off for good) copies sector by sector through a 4 KB RAM
  buffer, skipping equal sectors, then watchdog_reboot. It must read
  nothing from the old image (.rodata is in flash):
  `scripts/check_fwupdate.py` (run by build.sh) follows its calls in the
  .dis and fails on 0x10xxxxxx literals, or if the image reaches the
  staging area. nvflash_erase/program/read: raw access, interrupts off
  per operation.
  newlib's localtime/scanf cost ~20 KB of SRAM, so the clock avoids them.
- `tools/hostbridge.py`: run a host program with the terminal as display,
  typing on the PC.
- CI (`.gitlab-ci.yml`): test -> build (matrix of targets) -> package ->
  release (tags `vX.Y.Z`). Pipeline status is not visible from here.

## Resource budget (v0.5.0, MACHINE_BANKS=2)

SRAM is the tight resource; peripherals and pins are mostly free.

- SRAM (520 KB): .text 165 KB, .data 26 KB (fonts, Z80 tables, video code
  copied to RAM), .bss 288 KB; `__end__` 0x20077f4c, so ~32 KB free below
  0x20080000 (no malloc user); pb2's palette (tables + code) moved it to
  0x200790d8, ~28 KB free; the qur QoL branch (taller SET-UP, fwpanel's
  FATFS, code) to 0x2007b570, ~18.6 KB free; qur.8's packed raster rows
  (fonts 10x20 and the bold faces in flash) back to 0x2007aa00, ~22 KB
  free. Scratch X: core 1's stack is in .bss, so
  nearly all 4 KB free; scratch Y: 2 KB core 0 stack, ~2 KB free. Biggest
  .bss: Z80 banks 96 KB + bank 0 64 KB, raster 60 KB, monitor `window`
  16 KB, screen 13 KB, setup 8 KB, line_buf 6.4 KB, drives 4.5 KB, UART
  RX ring 4 KB, fwupdate `sector` 4 KB. A third Z80 bank (48 KB) does not
  fit. bd 3of: share the monitor's window with fwupdate (~5 KB back).
- Flash (4 MB): image 0-0x6e980 (442 KB); free to 2 MB (image growth;
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
  (PSRAM; GP8 can carry its select).
- GPIO: GP0/1 UART0, GP2/3 CTS/RTS (only with SERIAL_FLOW_CONTROL), GP4-7
  SD, GP12-19 DVI Sock, GP25 LED (VIDEO_DIAG only), GP23/24/29 board
  internal. Free: GP8-11, GP20-22, GP26-28 (ADC capable), and GP2/3 when
  flow control is off.

## Conventions & Patterns

- Style: C11, tabs, K&R braces, comments explain why; match surrounding
  code. Portable modules must not include pico-sdk headers (host-tested).
- Hardware constraints learned the hard way:
  - Code must run from SRAM (`COPY_TO_RAM`): the boot XIP setup assumes
    clk_sys <= 150 MHz (on the RP2040, flash execution hung above ~252 MHz:
    dmt.10).
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
- User preferences: flow control off by default; 80-column mode stays
  centred (80 px borders); smooth scrolling, double-size lines and RP2350
  HSTX are deferred nice-to-haves.
