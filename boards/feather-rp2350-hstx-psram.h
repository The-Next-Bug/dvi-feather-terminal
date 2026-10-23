// Adafruit Feather RP2350 with HSTX Port and its 8 MB PSRAM fitted
// (APS6404L, 64 Mbit, CS on GP8): feather-rp2350-hstx.h's pins, with the
// Z80 machine's banks in the PSRAM instead of SRAM (PSRAM_BANKS: 15 banks
// of 48 KB, the most port 64 can report; 96 KB of SRAM freed). The SDK's
// hardware_psram finds the chip at boot; without one the machine has no
// banks (CP/M 2.2 runs, CP/M 3 needs one). MEM T tests the banks CP/M
// does not use. boards/example.h explains each setting.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FEATHER_RP2350_HSTX_PSRAM_H
#define _BOARDS_FEATHER_RP2350_HSTX_PSRAM_H

pico_board_cmake_set(PSRAM_BANKS, 1)
// feather-rp2350-hstx.h's video mode (354 MHz), restated.
pico_board_cmake_set(BOARD_HSTX_MODE, 2)

// CMake follows only #include "boards/..." for these lines, which the
// compiler cannot find from here: the SDK's adafruit_feather_rp2350.h's,
// restated.
pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (8 * 1024 * 1024))
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)

#include "feather-rp2350-hstx.h"

#endif
