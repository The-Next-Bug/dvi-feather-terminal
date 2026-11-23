# Third-party components

The firmware image (`<target>/FIRMWARE.UF2` / `dvi_terminal.bin` in the release ZIP) contains code and data
from the projects below. Their license texts are in `licenses/` in the
release package (`scripts/package.sh` copies them from the sources the build
used). The project's own code is under the MIT license, in `LICENSE`.

| Component | Used for | License | Text |
|---|---|---|---|
| [Raspberry Pi pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.3.1 | Runtime, hardware drivers; `src/video/hstx_dvi.c` follows its pico-examples HSTX DVI example (same license) | BSD-3-Clause | `licenses/pico-sdk-BSD-3-Clause.txt` |
| CMSIS headers (in pico-sdk) | Cortex-M33 core definitions | Apache-2.0 | `licenses/Apache-2.0.txt` |
| [TinyUSB](https://github.com/hathach/tinyusb) (`lib/tinyusb`) | USB host: keyboards, USB sticks | MIT | `licenses/tinyusb-MIT.txt` |
| [z80pack](https://github.com/udo-munk/z80pack) | Z80 CPU core, disk controller, assembler, CP/M and disk images, vendored (`lib/z80pack`, see its `UPSTREAM.md`); `src/machine/machine.c` and `src/machine/z80pack/` follow its picosim | MIT | `licenses/z80pack-MIT.txt` |
| z80pack `z80core/log.h` (David McNaughton, from ESP-IDF) | Logging macros compiled into the CPU core | Apache-2.0 | `licenses/Apache-2.0.txt` |
| [FatFs](http://elm-chan.org/fsw/ff/) R0.15a (ChaN) | FAT file system on the SD card (`lib/fatfs`) | 1-clause BSD | `licenses/FatFs-BSD-1-Clause.txt` |
| [Terminus Font](https://terminus-font.sourceforge.net/) 8x16 (Dimitar Toshkov Zhekov) | 80-column glyphs (the generated table is a derivative) | SIL OFL 1.1 | `licenses/Terminus-OFL-1.1.txt` |
| misc-fixed 6x12, 6x10 and 6x13 (X.Org) | 132-column glyphs | Public domain | - |
| newlib and libgcc (Arm GNU Toolchain) | C library and compiler runtime | newlib: BSD-style licenses; libgcc: GPL-3.0 with the GCC Runtime Library Exception | in the toolchain's `license.txt` |

## Built-in CP/M disk images

The firmware carries three disk images from z80pack, compressed, which the
Z80 machine's monitor can write to an SD card:

- `CPM22` (z80pack `picosim/disks/cpm22.dsk`): Digital Research CP/M 2.2
  and its utilities, with z80pack's tools; its system tracks hold CP/M's
  CCP and BDOS moved to 62K and our BIOS and boot loader (`src/machine/cpm`,
  derived from z80pack picosim's, MIT). CP/M is © Digital
  Research / Caldera. In 2001 Lineo (Caldera's successor) granted the right
  to use, distribute and modify the CP/M material made available by "The
  Unofficial CP/M Web Site"; in July 2022 Bryan Sparks for DRDOS, Inc.,
  the owner of Digital Research's assets, restated it without that limit:
  "a right to use, distribute, modify, enhance, and otherwise make
  available in a nonexclusive manner CP/M and its derivatives"
  (http://www.cpm.z80.de/license.html). z80pack and this firmware
  redistribute CP/M on that basis. It is a permission statement, not a
  standard open-source license.
- `Z80TESTS` (z80pack `cpmsim/disks/library/z80tests.dsk`): Z80 instruction
  exercisers `EXZ80DOC`, `EXZ80ALL` and `PRELIM` (© 1994 Frank D. Cringle,
  GPL-2.0 or later, as modified for z80pack; `EXZ80ALL` is built by us from
  the same source with all flags tested; their sources `EX.MAC`,
  `EXZ80DOC.MAC`, `EXZ80ALL.MAC` and `PRELIM.MAC` are on the same disk; license text
  `licenses/GPL-2.0.txt`), and `CPUTEST` (SuperSoft Diagnostics II), as
  distributed with z80pack.
- `EMPTY`: a blank, formatted CP/M disk.
- `F83` (`lib/f83`, the files of F83V2-80.ARK, `lib/f83/UPSTREAM.md`):
  F83, FORTH-83 version 2.0 for CP/M-80, by Henry Laxen and Michael Perry,
  public domain ("may be freely distributed and copied, as long as the
  authors are given credit and no copyright notice is placed upon it",
  `README.80`).
- `WS4` (`lib/wordstar`): MicroPro International's WordStar 4.00 for
  CP/M-80, installed for the VT100, its `WS.OVR` and `WS*.COM` patched by
  `src/machine/cpm/ws4/ws4_patch.py`. Copyright MicroPro International;
  no license comes with these files. WordStar for CP/M is effectively
  abandonware: MicroPro is long gone, and the CP/M versions have not
  been sold or supported for decades.

## WiFi modem firmware

`modem/zimodem.bin` in the release ZIP is a separate program: the firmware
for the ESP8266 WiFi modem a board can have on its serial port, not part of
the terminal's firmware. It is our build of Zimodem (`lib/zimodem`), made
by `scripts/build-zimodem.sh` with the ESP8266 Arduino core:

- our fork: https://github.com/arlaneenalra/Zimodem/tree/frankless-build
- the original: https://github.com/bozimmerman/Zimodem

Its license texts are in `modem/licenses/` (`scripts/build-zimodem.sh`
copies them from the sources and the toolchain the build used), its
sources in `modem/sources/`: Zimodem's at the commit built
(`zimodem-<commit>.zip`) and the ESP8266 core's release archive
(`esp8266-2.7.4.zip`).

| Component | Used for | License | Text |
|---|---|---|---|
| [Zimodem](https://github.com/bozimmerman/Zimodem) (Bo Zimmerman), [our fork](https://github.com/arlaneenalra/Zimodem/tree/frankless-build) with the Frankless changes | The modem: Hayes commands, WiFi, telnet, printing | Apache-2.0, with its NOTICE | `modem/licenses/Zimodem-Apache-2.0.txt`, `Zimodem-NOTICE.txt` |
| [ESP8266 core for Arduino](https://github.com/esp8266/Arduino) 2.7.4, with its ESP8266WiFi library | Runtime, WiFi and network API | LGPL-2.1 | `modem/licenses/esp8266-arduino-LGPL-2.1.txt` |
| Espressif ESP8266 NONOS SDK (in the core) | WiFi driver, system libraries | Espressif MIT License (ESP8266 only) | `modem/licenses/Espressif-MIT.txt` |
| [lwIP](https://savannah.nongnu.org/projects/lwip/) 2 (in the core) | TCP/IP stack | BSD-3-Clause | `modem/licenses/lwIP-BSD-3-Clause.txt` |
| umm_malloc (Ralph Hempel, in the core) | Heap | MIT | `modem/licenses/umm_malloc-MIT.txt` |
| SPIFFS (Peter Andersson, in the core) | File system for Zimodem's settings | MIT | `modem/licenses/SPIFFS-MIT.txt` |
| eboot (Ivan Grokhotkov, in the core) | Boot loader at the start of the image | BSD-3-Clause | `modem/licenses/eboot-BSD-3-Clause.txt` |
| newlib, libgcc, libstdc++ (the core's Xtensa GCC toolchain) | C and C++ libraries, compiler runtime | newlib: BSD-style licenses; libgcc, libstdc++: GPL-3.0 with the GCC Runtime Library Exception | in the toolchain's sources |

Zimodem's ZMODEM (from Synchronet), HostCM (Robert Ferguson's) and SSH
(libssh2, BSD-3-Clause) code is built only for the ESP32 and is not in
this image.

The ESP8266 core is under the LGPL-2.1 and linked statically into the
image. Its complete source at the version used is in
`modem/sources/esp8266-2.7.4.zip`, the core's 2.7.4 release
(https://github.com/esp8266/Arduino/releases/tag/2.7.4, the archive
arduino-cli installs, pinned by `lib/zimodem/toolchain.env`); with
Zimodem's source, also there, and `scripts/build-zimodem.sh` the image can
be rebuilt and relinked, with a changed core if wanted.

### WiFi modem firmware: XH-C3F

`modem/xhc3f/zimodem-xhc3f.bin` is the same Zimodem fork built for the
Sparkleiot XH-C3F, an ESP32-C3 module (`MODULE=xhc3f
scripts/build-zimodem.sh`), with the ESP32 Arduino core 2.0.17 and the
ESP-IDF 4.4.7 it is built on. Its license texts are in
`modem/xhc3f/licenses/`: copied from Zimodem and the core's headers, and
fetched by the build, at fixed SHA-256s, where the core does not carry
them (ESP-IDF's LICENSE and COPYRIGHT.rst, wpa_supplicant's, newlib's,
SPIFFS's, the core's own LGPL-2.1 text). Its sources are in
`modem/xhc3f/sources/`.

| Component | Used for | License | Text |
|---|---|---|---|
| [Zimodem](https://github.com/bozimmerman/Zimodem), [our fork](https://github.com/arlaneenalra/Zimodem/tree/frankless-build) | The modem | Apache-2.0, with its NOTICE | `Zimodem-Apache-2.0.txt`, `Zimodem-NOTICE.txt` |
| libssh2 (in Zimodem) | SSH client | BSD-3-Clause | `libssh2-BSD-3-Clause.txt` |
| [ESP32 core for Arduino](https://github.com/espressif/arduino-esp32) 2.0.17 | Runtime, WiFi and network API | LGPL-2.1 | `arduino-esp32-LGPL-2.1.txt` |
| [ESP-IDF](https://github.com/espressif/esp-idf) 4.4.7 (Espressif; precompiled in the core), with its WiFi and PHY libraries and Mbed TLS | System, drivers, WiFi, TLS | Apache-2.0; `COPYRIGHT.rst` lists the third-party parts and their licenses | `ESP-IDF-Apache-2.0.txt`, `ESP-IDF-COPYRIGHT.rst` |
| FreeRTOS (in ESP-IDF) | Kernel | MIT | `FreeRTOS-MIT.txt` |
| lwIP (in ESP-IDF) | TCP/IP stack | BSD-3-Clause | `lwIP-BSD-3-Clause.txt` |
| wpa_supplicant (in ESP-IDF) | WiFi security | BSD | `wpa_supplicant-BSD.txt` |
| SPIFFS (in ESP-IDF) | File system for Zimodem's settings | MIT | `SPIFFS-MIT.txt` |
| FatFs (ChaN, in ESP-IDF) | FAT file system | BSD-style (ChaN's) | `FatFs-ChaN.txt` |
| cJSON, nghttp2 (in ESP-IDF) | JSON, HTTP/2 (linked by ESP-IDF's components) | MIT | `cJSON-MIT.txt`, `nghttp2-MIT.txt` |
| protobuf-c (in ESP-IDF) | Provisioning messages (linked by ESP-IDF's components) | BSD-2-Clause | `protobuf-c-BSD-2-Clause.txt` |
| newlib, libgcc, libstdc++ (ESP-IDF's RISC-V GCC toolchain) | C and C++ libraries, compiler runtime | newlib: BSD-style licenses; libgcc, libstdc++: GPL-3.0 with the GCC Runtime Library Exception | `newlib-COPYING.NEWLIB.txt`; the rest in the toolchain's sources |

The ESP32 core is under the LGPL-2.1 and linked statically into the image.
Its source at the version used is in
`modem/xhc3f/sources/arduino-esp32-2.0.17-lgpl.zip`: the core's 2.0.17
release archive (https://github.com/espressif/arduino-esp32/releases/tag/2.0.17,
the one arduino-cli installs, pinned by `lib/zimodem/toolchain.env`; its
SHA-256 is in `build-info.txt`) without `tools/sdk/`, the four chips'
precompiled ESP-IDF libraries (Apache-2.0, not LGPL; in the full archive),
and the Windows tools. With Zimodem's source, also there, the full
archive and `scripts/build-zimodem.sh` the image can be rebuilt and
relinked, with a changed core if wanted.
