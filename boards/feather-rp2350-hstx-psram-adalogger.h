// Adafruit Feather RP2350 with HSTX Port, its 8 MB PSRAM fitted, and an
// Adalogger FeatherWing: feather-rp2350-hstx-psram.h (the Z80 machine's
// banks in the PSRAM, PSRAM_BANKS) with the FeatherWing's PCF8523
// real-time clock on I2C1 GP2/3, as in feather-rp2350-hstx-adalogger.h,
// which explains it: no flow control, no 1 Hz tick. boards/example.h
// explains each setting.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FEATHER_RP2350_HSTX_PSRAM_ADALOGGER_H
#define _BOARDS_FEATHER_RP2350_HSTX_PSRAM_ADALOGGER_H

// CMake follows only #include "boards/..." for these lines, which the
// compiler cannot find from here: feather-rp2350-hstx-psram.h's and the
// SDK's adafruit_feather_rp2350.h's, restated.
pico_board_cmake_set(PSRAM_BANKS, 1)
pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (8 * 1024 * 1024))
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
// feather-rp2350-hstx.h's video mode (354 MHz), restated.
pico_board_cmake_set(BOARD_HSTX_MODE, 2)

#include "feather-rp2350-hstx-psram.h"
#include "feather-rp2350-hstx-adalogger.h"

#endif
