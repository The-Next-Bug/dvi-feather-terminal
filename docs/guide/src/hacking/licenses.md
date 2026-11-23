# Licenses

This project's own code and documentation are under the MIT license
(`LICENSE`). Files derived from other projects keep their original notices:
`src/video/hstx_dvi.c` from pico-examples (BSD-3-Clause);
`src/machine/machine.c`, `src/machine/z80pack/` and `src/machine/cpm/` from
z80pack's picosim (MIT).

| Component | License |
|---|---|
| Terminus Font (the 80-column fonts) | SIL Open Font License 1.1 (`assets/fonts/Terminus-OFL.txt`); the tables generated from it stay under it |
| misc-fixed 6x12 (the 132-column font) | public domain |
| pico-sdk | BSD-3-Clause; its CMSIS headers Apache-2.0 |
| TinyUSB | MIT |
| z80pack (the Z80 core, picosim, z80asm) | MIT; its CPU core's `log.h` Apache-2.0 |
| FatFs | ChaN's 1-clause BSD license |
| F83 | public domain |
| WordStar 4.00 | MicroPro International's; no license comes with the files (`docs/THIRD-PARTY.md`) |
| CP/M 2.2 and CP/M 3 | Digital Research's, redistributed under the right to use, distribute and modify CP/M and its derivatives that DRDOS, Inc. stated in 2022, widening Lineo's 2001 grant to The Unofficial CP/M Web Site |
| the Z80 exercisers | Frank D. Cringle's, GPL-2.0 or later (their sources are on the same disk image) |

`THIRD-PARTY.md` in the release ZIP lists every third-party component in
the firmware with its license, and `licenses/` holds the texts.

## The WiFi modem's firmware

The release ZIP's `modem/` holds a second program, the
[WiFi modem](../terminal/wifi-modem.md)'s firmware: our build of Zimodem
(`lib/zimodem`, `scripts/build-zimodem.sh`; [our fork](https://github.com/arlaneenalra/Zimodem/tree/frankless-build) of
[the original](https://github.com/bozimmerman/Zimodem)). It has its own licenses, in `modem/licenses/`,
and its sources in `modem/sources/`: Zimodem's at the commit built and
the ESP8266 core's release archive, the LGPL part's complete source:

| Component | License |
|---|---|
| Zimodem (Bo Zimmerman; our fork) | Apache-2.0, with its NOTICE |
| ESP8266 core for Arduino 2.7.4 | LGPL-2.1, linked statically; rebuild and relink with `scripts/build-zimodem.sh` |
| Espressif's ESP8266 NONOS SDK | Espressif MIT License, for the ESP8266 only |
| lwIP | BSD-3-Clause |
| umm_malloc, SPIFFS | MIT |
| eboot (the boot loader) | BSD-3-Clause |
| newlib, libgcc, libstdc++ | BSD-style; GPL-3.0 with the GCC Runtime Library Exception |

The XH-C3F's build in `modem/xhc3f/` (an ESP32-C3) is Zimodem on the
ESP32 Arduino core and Espressif's ESP-IDF, with its own `licenses/` and
`sources/`: Zimodem's, and the core's LGPL part (its release archive
without the precompiled ESP-IDF libraries):

| Component | License |
|---|---|
| Zimodem (Bo Zimmerman; our fork), with its libssh2 (SSH) | Apache-2.0, with its NOTICE; libssh2 BSD-3-Clause |
| ESP32 core for Arduino 2.0.17 | LGPL-2.1, linked statically; rebuild and relink with `scripts/build-zimodem.sh` |
| ESP-IDF 4.4.7 (Espressif), with Mbed TLS | Apache-2.0; its `COPYRIGHT.rst` lists the parts under other licenses |
| FreeRTOS, SPIFFS, cJSON, nghttp2 | MIT |
| lwIP | BSD-3-Clause |
| wpa_supplicant, protobuf-c, FatFs | BSD-style (FatFs: ChaN's) |
| newlib, libgcc, libstdc++ | BSD-style; GPL-3.0 with the GCC Runtime Library Exception |

`THIRD-PARTY.md`'s "WiFi modem firmware" section has the details.
