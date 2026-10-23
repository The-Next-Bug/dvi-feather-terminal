// Adafruit Feather RP2350 with HSTX Port and an Adalogger FeatherWing
// (product 2922: microSD, PCF8523 real-time clock, CR1220 holder):
// feather-rp2350-hstx.h's pins, the FeatherWing's SD card where they put
// it (SPI0, CS on D10), and its PCF8523 on the Feather's SDA/SCL (I2C1,
// GP2/3, also the STEMMA QT connector). The PCF8523 is the cheaper,
// less exact RTC (about +-2 s/day; years to 2099); with a cell in the
// holder it keeps the time over power-off. boards/example.h explains each
// setting.
//
// No flow control: CTS/RTS were GP2/3, the RTC's I2C now
// (SERIAL_FLOW_CONTROL builds stop at board.h's check). No 1 Hz tick: the
// FeatherWing's INT/SQW pad is not wired to a Feather pin, so the
// firmware's timer keeps the time between start-up and a set.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FEATHER_RP2350_HSTX_ADALOGGER_H
#define _BOARDS_FEATHER_RP2350_HSTX_ADALOGGER_H

// CMake follows only #include "boards/..." for these lines, which the
// compiler cannot find from here: the SDK's adafruit_feather_rp2350.h's,
// restated.
pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (8 * 1024 * 1024))
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
// feather-rp2350-hstx.h's video mode (354 MHz), restated.
pico_board_cmake_set(BOARD_HSTX_MODE, 2)

#include "feather-rp2350-hstx.h"

#undef BOARD_UART_CTS_PIN
#undef BOARD_UART_RTS_PIN

// --- Real-time clock (PCF8523, I2C1) ---
#define BOARD_RTC_PCF8523 1
#define BOARD_RTC_I2C 1
#define BOARD_RTC_SDA_PIN 2
#define BOARD_RTC_SCL_PIN 3

#endif
