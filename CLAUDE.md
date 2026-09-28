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
git submodule update --init          # lib/pico-sdk (2.3.1), lib/PicoDVI (not their submodules)
git -C lib/pico-sdk submodule update --init --depth 1 lib/tinyusb   # only nested one used
scripts/setup-toolchain.sh           # pinned CMake, Arm GNU Toolchain, picotool (toolchain.env)
scripts/test.sh                      # host unit tests (gcc + ASan/UBSan): screen, vtparse, term, raster, keyboard
scripts/build.sh                     # TARGET=feather-rp2040-dvi (default) -> build/<target>/dvi_terminal.uf2
TARGET=pico2-dvi-sock scripts/build.sh   # RP2350: Pico 2 + Pico DVI Sock
scripts/package.sh                   # every built target -> dist/dvi-terminal-<describe>.zip
```

Build options (CMake; `scripts/build.sh` passes `SERIAL_FLOW_CONTROL` from
the environment, other options via `CMAKE_ARGS`):
`SERIAL_FLOW_CONTROL` (RTS/CTS on GP3/GP2, default OFF), `STARTUP_DEMO`
(default ON), `VIDEO_DIAG` (UART0 TX diagnostics, default OFF; also prints
panic() text), `USB_KEYBOARD` (TinyUSB host keyboard; default ON for RP2350,
OFF for RP2040, where it crashed: dmt.20),
`COPY_TO_RAM` (default ON; must stay ON, see below).

Always run `scripts/test.sh` and a firmware build before handing work
back. Hardware checks are done by the user (no board is attached to the
dev host; Docker is not usable for CI simulation).

## Architecture Overview

VT100 serial terminal with DVI output for the Adafruit Feather RP2040 DVI
and a Pico 2 with the Pico DVI Sock. 800x600p60 (400 MHz, 1.3 V); 80 or 132
columns x 24 rows plus a status line (row 24); UART0 on GP0/GP1.

- `src/screen.[ch]` (portable): cell buffer: glyph code, RGB222 fg/bg and
  attribute bits per cell; `row_map` (display row -> line slot) so
  scrolling rotates indices; `dirty` bit per slot; `reverse_rows` mask.
- `src/vtparse.[ch]` (portable): Paul Williams' DEC parser. C1 (0x80-0x9F)
  ignored unless `c1_controls` (keeps UTF-8 safe); OSC/DCS/APC discarded.
- `src/term.[ch]` (portable): VT100/VT102 emulation on the screen; replies
  (DA, DSR/CPR, DECREQTPARM) through a callback.
- `src/raster.[ch]` (portable): screen line -> pixel buffers for the PicoDVI
  `tmds_encode_font_2bpp` encoder, fed an identity "font" row so each byte
  is its own 8-pixel pattern. Per 8-px group and colour plane the encoder
  has two levels; raster picks them per group (exact for 80 cols, exact in
  the usual 132-col cases). Stored rows per line = font rows + 2 padding
  rows; connectors (line drawing, blocks, shades) extend into the padding.
  Handles underline, blink phase, bold (emboldened), reverse, cursor cell.
- `src/video.[ch]` (RP2040/RP2350): libdvi setup, core 1 scanline loop
  (encode 832 px/line into TMDS buffers with slack, cursor inversion),
  `video_update()` on core 0 rasterises dirty slots, cursor slot and blink.
- `src/keyboard.[ch]` (portable): HID boot keyboard reports -> VT100 key
  codes (DECCKM, DECKPAM, LNM, Num/Caps Lock, autorepeat via kbd_tick).
- `src/usbkbd.[ch]` + `src/tusb_config.h` (USB_KEYBOARD builds, RP2350 by
  default): TinyUSB host on the native USB port, boot keyboards only; keys
  go to the serial TX queue. The board does not power VBUS in host mode
  (5 V fed into the VBUS pin).
- `src/serial.[ch]`: UART0 RX ring (IRQ) + TX queue, optional hardware
  RTS/CTS. `src/demo.[ch]`: startup demo. `src/diag.[ch]`: VIDEO_DIAG.
- `src/font_gen.py` + `cmake/fonts.cmake`: 256-glyph tables (Terminus 8x16,
  misc-fixed 6x13) generated at build time; glyph codes in `src/font.h`.
- `tools/hostbridge.py`: run a host program with the terminal as display,
  typing on the PC.
- CI (`.gitlab-ci.yml`): test -> build (matrix of targets) -> package ->
  release (tags `vX.Y.Z`). Pipeline status is not visible from here.

## Conventions & Patterns

- Style: C11, tabs, K&R braces, comments explain why; match surrounding
  code. Portable modules must not include pico-sdk headers (host-tested).
- Hardware constraints learned the hard way:
  - Code must run from SRAM (`COPY_TO_RAM`): boot2 reads flash with 03h at
    clk_sys/4 and flash execution hangs above ~252 MHz (dmt.10).
  - Core 1 must never touch flash or `const` data (which lands in flash):
    keep its data non-const/static and its code `__not_in_flash_func`.
  - `tmds_encode_font_2bpp` processes 64 px per loop pass: widths must be
    multiples of 64; it overruns otherwise (hence the TMDS buffer slack).
  - The 2 KB core 0 stack is small: big scratch arrays are static.
  - RP2040 heap = SRAM left after .bss (code is in SRAM too). libdvi's
    dvi_init mallocs 3 x 4.8 KB TMDS buffers, which video.c swaps for
    heap-allocated ones with slack: ~15 KB of heap is needed at boot, and
    malloc failure panics silently (crash before video; RP2350 unaffected).
    Check `__end__` in the .elf.map when adding static data or libraries.
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
