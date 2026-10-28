# Building from source

Everything installs under `.toolchain/` in the repository (git-ignored);
the host needs only `curl`, `git`, `python3`, `xz` and a C/C++ compiler
with `make`.

```sh
git submodule update --init      # pico-sdk and TinyUSB (not pico-sdk's own submodules)
scripts/setup-toolchain.sh       # CMake, Arm GNU Toolchain, picotool, clang-tidy, mdBook
scripts/test.sh                  # host unit tests
scripts/build.sh                 # -> build/pico2-dvi-sock/dvi_terminal.uf2
scripts/lint.sh                  # clang-tidy over the tests' and the firmware's builds
scripts/docs.sh                  # this guide -> build/docs/html/index.html
scripts/package.sh               # every built target -> dist/dvi-terminal-<rev>.zip
```

## scripts/build.sh

| Variable | Meaning |
|---|---|
| `TARGET` | the board: `boards/<TARGET>.h` (default `pico2-dvi-sock`; see [Boards](../start/boards.md)) |
| `EXPERIMENTAL=1` | allow a header from `boards/experimental/` |
| `BUILD_DIR` | output root (default `build`; the build lands in `BUILD_DIR/TARGET`) |
| `CMAKE_ARGS` | extra CMake arguments, e.g. `-DDVI_MODE=800x600` |
| `SDCARD` | a mounted SD card to copy `FIRMWARE.UF2` to |

It writes `dvi_terminal.uf2`, `.bin`, `.elf`, the disassembly and map,
`FIRMWARE.UF2` and `build-info.txt`, and runs `scripts/check_sram_only.py`,
which fails the build if code that must never read the flash could reach
code or data left there (the firmware update's final copy, the video core,
the clock change, the flash writes).

## Build options

| CMake option | Default | Meaning |
|---|---|---|
| `DVI_MODE` | the board's, else `1024x600` | the Display setting's default until one is saved, and `hstx_test`'s mode: `1024x600` or `800x600` |
| `STARTUP_LOGO` | ON | the startup logo |
| `LOGO_IMAGE` | `assets/logo/ColorLogo-576.png` | the logo's PNG: non-interlaced 8-bit, at most 800x576, a multiple of 8 wide, one hue on black |
| `LOGO_MS` | 10000 | how long the logo shows, in ms |
| `LOGO_IN_FLASH` | ON | keep the logo's data in flash (saves about 19 KB of SRAM) |
| `STARTUP_DEMO` | OFF | run the demo after the logo until input |
| `MACHINE_BANKS` | 2 | the Z80's 48 KB banks besides bank 0, in SRAM (each costs 48 KB; a third does not fit) |
| `PSRAM_BANKS` | the board's | the banks in the board's PSRAM instead (up to 15), and the rest a RAM disk |
| `VIDEO_DIAG` | OFF | bring-up diagnostics on the UART's TX: boot markers, the video core's timing, fault reports |
| `COPY_TO_RAM` | ON | run from SRAM; must stay ON |
| `USB_WA_*` | see `CMakeLists.txt` | workarounds for TinyUSB's RP2350 host (`USB_WA_POLL_PAUSE` ON by default) |
| `HSTX_MODE` | | retired after v0.10.0: CMake refuses it and names `DVI_MODE` |

## Toolchain versions

Pinned in `toolchain.env`, the single source of truth for
`scripts/setup-toolchain.sh`: CMake, the Arm GNU Toolchain, picotool,
clang-tidy and mdBook, each with its SHA-256. To upgrade one, change its
version and checksum there and the matching `ARG`s of the CI build image.
pico-sdk and TinyUSB are submodules under `lib/`; z80pack (the Z80 core and
picosim's disks), FatFs and F83 are vendored.

## This guide

`scripts/docs.sh` builds the guide: it builds `docgen` with the host tests
(`docs/guide/gen/docgen.c`), which renders the screenshots with the
firmware's own display code and writes the settings tables; turns the
screenshots into PNGs; writes the tables from headers (`gen_tables.py`:
the board pins, the hardware information items); runs mdBook over
`docs/guide/src`; and checks every link. Pages are Markdown in
`docs/guide/src/`, the table of contents is `SUMMARY.md`.
