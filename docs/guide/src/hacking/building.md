# Building from source

Everything installs under `.toolchain/` in the repository (git-ignored);
the host needs only `curl`, `git`, `python3`, `xz` and a C/C++ compiler
with `make`.

```sh
git submodule update --init      # pico-sdk, TinyUSB and Zimodem (not pico-sdk's own submodules)
scripts/setup-toolchain.sh       # CMake, Arm GNU Toolchain, picotool, clang-tidy, mdBook, arduino-cli + ESP8266 core
scripts/test.sh                  # host unit tests
scripts/build.sh                 # -> build/pico2-dvi-sock/dvi_terminal.uf2
scripts/lint.sh                  # clang-tidy over the tests' and the firmware's builds
scripts/docs.sh                  # this guide -> build/docs/html/index.html
scripts/build-zimodem.sh         # the WiFi modem's firmware -> build/zimodem/zimodem.bin
MODULE=xhc3f scripts/build-zimodem.sh  # the XH-C3F's -> build/zimodem-xhc3f/zimodem-xhc3f.bin
scripts/package.sh               # every built target and the modem's -> dist/dvi-terminal-<rev>.zip
```

`scripts/setup-toolchain.sh` takes components by name to install only
some: `cmake arm picotool clang-tidy mdbook zimodem`. `zimodem` (about
400 MB, the Xtensa compiler with it) is needed only for
`scripts/build-zimodem.sh`. `zimodem-esp32` (the ESP32 core, about 2 GB
more) is only installed when named, for `MODULE=xhc3f`.

## scripts/build-zimodem.sh

Builds the [WiFi modem](../terminal/wifi-modem.md)'s firmware from
`lib/zimodem` (our Zimodem fork, branch `frankless-build`) with its own
`scripts/build.sh` and the pinned arduino-cli and ESP8266 core
(`lib/zimodem/toolchain.env`), into `build/zimodem/`: `zimodem.bin` (an
ESP-12F's image, written at 0x0), `ZIMODEM.ZMI` (the same in the
[container](../reference/file-formats.md#zimodemzmi) the terminal will flash
from), `build-info.txt`, the
[license texts](licenses.md#the-wifi-modems-firmware) in `licenses/` and
the sources in `sources/` (Zimodem's at the commit, and the ESP8266 core's
release archive, from arduino-cli's download or fetched again, checked
against the package index's SHA-256). The version Zimodem reports (`ATI4`) is
`4.0.3-` and the submodule commit's time, so one commit always builds the
same version. `BUILD_DIR`, `BUILD_ID` and `FRANKLESS_HW_FLOW` override.
`scripts/package.sh` puts it in the ZIP's `modem/` (CI requires it there:
`REQUIRE_ZIMODEM=1`).

`MODULE=xhc3f` builds for the XH-C3F (ESP32-C3) instead, with the ESP32
core 2.0.17, into `build/zimodem-xhc3f/`: `zimodem-xhc3f.bin` (boot loader,
partition table and Zimodem merged, written at 0x0), the parts beside it,
`build-info.txt`, `licenses/` (from the core's headers, and texts the core
lacks fetched from ESP-IDF 4.4.7 and the others at fixed SHA-256s) and
`sources/` (Zimodem's, and the core's release archive without the
precompiled ESP-IDF libraries: its LGPL part). No `ZIMODEM.ZMI`: the
terminal cannot flash a C3 yet. `scripts/package.sh` puts it in
`modem/xhc3f/`.

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
| `USB_WA_*` | see `CMakeLists.txt` | workarounds for TinyUSB's RP2350 host (all OFF by default) |
| `HSTX_MODE` | | retired after v0.10.0: CMake refuses it and names `DVI_MODE` |

## Toolchain versions

Pinned in `toolchain.env`, the single source of truth for
`scripts/setup-toolchain.sh`: CMake, the Arm GNU Toolchain, picotool,
clang-tidy and mdBook, each with its SHA-256. To upgrade one, change its
version and checksum there; CI's toolchain cache is keyed on the file, so
the next run installs the new one.
pico-sdk and TinyUSB are submodules under `lib/`; z80pack (the Z80 core and
picosim's disks), FatFs, F83 and WordStar 4 (`lib/wordstar`) are vendored.

## This guide

`scripts/docs.sh` builds the guide: it builds `docgen` with the host tests
(`docs/guide/gen/docgen.c`), which renders the screenshots with the
firmware's own display code and writes the settings tables; turns the
screenshots into PNGs; writes the tables from headers (`gen_tables.py`:
the board pins, the hardware information items); runs mdBook over
`docs/guide/src`; and checks every link. Pages are Markdown in
`docs/guide/src/`, the table of contents is `SUMMARY.md`.
