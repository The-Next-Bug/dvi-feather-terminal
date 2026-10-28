# Source map

The code is in libraries, one directory each under `src/` (static
libraries `dvt_<name>`): a library's public headers sit next to its
sources. A library is either portable (no pico-sdk; the host tests build
it) or RP2350-only.

| Library | Kind | Contents | Described in |
|---|---|---|---|
| `src/common` | portable | `crc32`, `lz` (the built-in disks' decompressor), `scratch` (a shared 4 KB buffer), `xonxoff` | [Connecting a host](../start/host.md#flow-control) |
| `src/clock` | portable | `datetime` (dates, FAT timestamps), `wallclock` (the time between reads of the RTC) | [Clock](../terminal/clock.md) |
| `src/rtc` | both | the RTC chip drivers `ds3231`, `pcf8523` (portable); `rtc` (the board's chip on I2C, RP2350) | [Clock](../terminal/clock.md) |
| `src/screen` | portable | `screen` (cells, saved rows), `palette`, `font.h` (glyph codes) | [Colors](../terminal/colors.md), [Character set](../reference/charset.md) |
| `src/term` | portable | `term` (the terminal), `vt100`, `vt52`, `adm3a` (its personalities), `vtparse` (the parser) | [Terminal types](../terminal/terminal-types.md), [VT100](../reference/vt100.md), [VT52 and ADM-3A](../reference/vt52-adm3a.md) |
| `src/render` | portable | `raster` (cells to pixels), `splash` (the logo); `font_gen.py`, `logo_gen.py` | [Displays](../start/displays.md), [How it works](how-it-works.md) |
| `src/input` | portable | `keyboard` (keys to codes), `keyname`, `blank` | [Keyboard](../terminal/keyboard.md), [Keys sent](../reference/keys.md) |
| `src/settings` | portable | `settings` (the records, the fields), `uf2` (firmware file checks) | [SET-UP](../terminal/setup.md), [Firmware updates](../terminal/firmware-update.md) |
| `src/machine` | portable | `machine` (the Z80 machine on z80pack's core); `cpm/` (the BIOSes and the CP/M tools, in assembler), `cpm/cpm3/`, `cpm/f83/`, `cpm/tunes/` | [The Z80 CP/M machine](../cpm/overview.md), [Z80 I/O ports](../reference/ports.md) |
| `src/sound` | portable | `ay8910`, `mixer`, `tune` | [Bell and sound](../terminal/sound.md) |
| `src/disks` | portable | `drives`, `drivecfg` (images, DRIVES.CFG, sets), `diskimg` (the built-in disks), `xfer` (port 10), `usbvol`, `sdformat` | [Drives](../cpm/drives.md), [Moving files](../cpm/moving-files.md), [File transfer](../reference/xfer.md) |
| `src/ui` | portable | `setup`, `clockpanel`, `drivepanel`, `fwpanel`, `monitor`, `demo` | [SET-UP](../terminal/setup.md), [The monitor](../cpm/monitor.md) |
| `src/board` | RP2350 | `board.h` (the board header, checked) | [Adding a board](boards.md) |
| `src/platform` | RP2350 | `serial`, `led`, `audio`, `nvflash` (the settings' flash), `fwupdate`, `extram` (PSRAM), `diag` | [Troubleshooting](../terminal/troubleshooting.md), [Firmware updates](../terminal/firmware-update.md) |
| `src/usb` | RP2350 | `usbkbd` (keyboards, the USB log, the watchdog), `tusb_config.h` | [Keyboard](../terminal/keyboard.md) |
| `src/storage` | RP2350 | `diskio` (FatFs drives: the SD card, a USB stick), `sdcard`, `usbmsc` | [SD card and USB sticks](../cpm/storage.md) |
| `src/video` | RP2350 | `hstx_dvi` (the DVI output, the modes), `video` (scanlines on core 1) | [Displays](../start/displays.md), [How it works](how-it-works.md) |

## Programs

| Path | Contents |
|---|---|
| `apps/terminal/` | the firmware: `main.c` (start-up, the main loop, the Display prompt), `setup_glue.c` (SET-UP and its panels), `cpm_glue.c` (the Z80 machine's platform), `status.c` (the status line), `input_glue.c` (keys), `hwinfo_glue.c` (port 12's records), `crash_glue.c`, `usblog_glue.c` |
| `apps/hstx_test/` | the HSTX test pattern |

## Elsewhere

| Path | Contents |
|---|---|
| `boards/` | the board headers |
| `test/` | the host tests |
| `docs/guide/` | this guide: `src/` (pages), `gen/` (its generators) |
| `scripts/` | build, test, lint, package, docs, toolchain |
| `tools/hostbridge.py` | run a PC program with the terminal as its display |
| `lib/` | pico-sdk and TinyUSB (submodules), z80pack, FatFs, F83 (vendored) |
| `assets/` | the fonts and the logo |
| `ld/` | the linker script override: the code that stays in flash |
