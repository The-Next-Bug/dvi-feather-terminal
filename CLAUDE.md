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
git submodule update --init          # lib/pico-sdk (2.3.1), lib/z80pack (not their submodules)
git -C lib/pico-sdk submodule update --init --depth 1 lib/tinyusb   # only nested one used
scripts/setup-toolchain.sh           # pinned CMake, Arm GNU Toolchain, picotool (toolchain.env)
scripts/test.sh                      # host unit tests (gcc + ASan/UBSan): screen, vtparse, term, raster, keyboard, keyname, settings, setup, splash, lz, machine (boots CP/M 2.2 from a FAT32 RAM volume)
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
`COPY_TO_RAM` (default ON; must stay ON, see below), `MACHINE_BANKS` (Z80
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

- `src/screen.[ch]` (portable): cell buffer: glyph code, RGB222 fg/bg and
  attribute bits per cell; `row_map` (display row -> line slot) so
  scrolling rotates indices; `dirty` bit per slot; `reverse_rows` mask.
- `src/vtparse.[ch]` (portable): Paul Williams' DEC parser. C1 (0x80-0x9F)
  ignored unless `c1_controls` (keeps UTF-8 safe); OSC/DCS/APC discarded.
- `src/term.[ch]` (portable): VT100/VT102 emulation on the screen; replies
  (DA, DSR/CPR, DECREQTPARM) through a callback.
- `src/raster.[ch]` (portable): `raster_render_line` (core 0) turns a
  screen line into, per stored row and cell, a byte of the cell's pixels
  (bit i = pixel i, set = fg) plus per-cell fg/bg (RGB222, reverse
  applied). Stored rows per line = font rows + 2 padding rows; connectors
  (line drawing, blocks, shades) extend into the padding. Handles
  underline, blink phase, bold (emboldened, within the cell), reverse.
  `raster_compose` (core 1) makes a scanline of RGB565 pixel pairs from a
  stored row through a 64-entry colour table, inverting the cursor cell;
  every cell is exact in both widths. 132 cols centred at x = 4.
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
  baud, framing, newline (LNM RIS value), colours, palette (mono: SGR
  colours -> brightness/inverse of the defaults, term_mono_colours; the
  SET-UP preview uses it too), mode (terminal / CP/M machine). CRC-32 in
  `src/crc32.[ch]` (shared with the disk images). main.c apply_settings()
  -> serial_configure, term_set_defaults, term_set_colours (recolours
  cells in the old defaults).
  `src/setup.[ch]` (portable): SET-UP panel over the bottom text rows
  (one row per setting, plus two preview rows under Palette: ANSI colours
  as fg and as bg, spaced so no 8-px group held two samples in 132 cols
  under the old PicoDVI encoder)
  (key bytes in, save/cancel out); main.c saves/restores those rows. Opened
  by F12 or Pause (keyboard.c SET-UP callback) or host `CSI ? 999 ~` (term.c bumps
  `setup_requests` and term_write stops after it; later bytes are keys).
- `src/keyname.[ch]` (portable): key bytes -> "ESC [A", "^C", ... for the
  status line's last-key field (which replaced the rx/tx/err counters).
- `src/nvflash.[ch]`: last 2 flash sectors. Read at boot before
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
- `src/font_gen.py` + `cmake/fonts.cmake`: 256-glyph tables (Terminus 8x16,
  misc-fixed 6x13) generated at build time; glyph codes in `src/font.h`.
- Z80 CP/M machine (Mode setting "CP/M machine", bd lpt): the screen and
  keyboard become the console of a z80pack picosim-compatible machine on
  core 0 (`main.c` cpm_*: 10 ms CPU slices between keyboard, disks, screen).
  `src/machine.[ch]` (portable): z80pack's Z80 core (`lib/z80pack`
  submodule, MIT; only z80core + iodevices/sd-fdc.c, built by
  `cmake/z80pack.cmake`) configured by `src/z80pack/{sim,simmem,simio,
  simport}.h`; picosim's port map (SIO1 0/1 console, SIO2 2/3 = UART0, FDC
  4, MMU 64, clock 65/66 without localtime, 60 Hz timer 67, hwctl 160) and
  boot ROM; bank 0 64 KB + MACHINE_BANKS x 48 KB. Slices end from
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
  writes the system tracks of CPM22 (README there). `src/monitor.[ch]`
  (portable): menu (boot, mount, list, new blank disk, install, speed
  test, rescan), 3 s autoboot. `src/drivepanel.[ch]` (portable): SET-UP's
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
  saves and reboots (watchdog). In SET-UP, M leaves for the monitor. F11
  is the machine's reset key in CP/M mode (kbd_set_reset_key; terminal
  mode keeps ESC [ 23 ~).
  SRAM: ~55 KB heap left with 2 banks; newlib's localtime/scanf cost ~20 KB,
  so the clock avoids them.
- `tools/hostbridge.py`: run a host program with the terminal as display,
  typing on the PC.
- CI (`.gitlab-ci.yml`): test -> build (matrix of targets) -> package ->
  release (tags `vX.Y.Z`). Pipeline status is not visible from here.

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
    panics silently. The RP2350 has room (raster[] is ~66 KB), but check
    `__end__` in the .elf.map when adding big static data or libraries.
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
