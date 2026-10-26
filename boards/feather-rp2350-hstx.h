// Adafruit Feather RP2350 with HSTX Port (product 6000): RP2350A, 8 MB
// flash, optional 8 MB PSRAM, and a 22-pin FPC connector on the HSTX pins
// for Adafruit's DVI breakout (a 22-pin cable to an HDMI socket). Pins from
// the pico-sdk's adafruit_feather_rp2350.h and Adafruit's CircuitPython
// board (ports/raspberrypi/boards/adafruit_feather_rp2350/pins.c); run on
// the board (bd 1ls.19.6). boards/example.h explains each setting.
//
// No SD slot: the SD card goes on SPI0 at the Feather's SCK/MOSI/MISO with
// CS on D10, where the Adalogger FeatherWing puts it (or an SD breakout
// wired the same way).
//
// USB host: the USB-C socket is the RP2350's USB, so a keyboard needs an
// OTG adapter, and the Feather does not power VBUS itself: feed 5 V into
// the USB pin. Flashing is over the same socket (BOOT + RESET).
//
// The board's other pins, which the firmware leaves alone: GP21 the
// NeoPixel, GP2/3 SDA/SCL (STEMMA QT, I2C1; also CTS/RTS below), D5/D6/D9/
// D11/D12/D24/D25 and A0-A3 on the headers.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FEATHER_RP2350_HSTX_H
#define _BOARDS_FEATHER_RP2350_HSTX_H

// --- Video mode ---
//
// 800x600 at 60 Hz with reduced blanking: clk_sys 354 MHz. At the usual
// 400 MHz (and 1.3 V) two Feathers crashed at random, with or without
// PSRAM, where the Pico 2 and Frankless do not (bd 1ls.19.8): likely the
// core supply's parts under that load. Core 1 has less time per line (in
// 132 columns up to 98-99% of it) and the Z80 runs about 11% slower. The
// headers that include this one restate it (CMake does not follow them).
pico_board_cmake_set(BOARD_HSTX_MODE, 2)

// --- Terminal UART ---
//
// UART0 on the Feather's TX/RX pins. CTS/RTS (Flow control: RTS/CTS) are
// the SDA/SCL pins, shared with the STEMMA QT connector: nothing may be
// plugged in there then.
#define BOARD_UART 0
#define BOARD_UART_TX_PIN 0
#define BOARD_UART_RX_PIN 1
#define BOARD_UART_CTS_PIN 2 // used with the Flow control setting's RTS/CTS
#define BOARD_UART_RTS_PIN 3

#define PICO_DEFAULT_UART BOARD_UART
#define PICO_DEFAULT_UART_TX_PIN BOARD_UART_TX_PIN
#define PICO_DEFAULT_UART_RX_PIN BOARD_UART_RX_PIN

// RP2350A, 8 MB flash, crystal, LED on GP7 (D13), PSRAM CS GP8.
#include "boards/adafruit_feather_rp2350.h"

// --- DVI (HSTX, GP12-19) ---
//
// The FPC connector's pairs: + on the even pin.
#define BOARD_DVI_CLK_P_PIN 14
#define BOARD_DVI_CLK_N_PIN 15
#define BOARD_DVI_D0_P_PIN 18
#define BOARD_DVI_D0_N_PIN 19
#define BOARD_DVI_D1_P_PIN 16
#define BOARD_DVI_D1_N_PIN 17
#define BOARD_DVI_D2_P_PIN 12
#define BOARD_DVI_D2_N_PIN 13

// --- SD card (SPI0) ---
#define BOARD_SD_SPI 0
#define BOARD_SD_MISO_PIN 20
#define BOARD_SD_CS_PIN 10
#define BOARD_SD_SCK_PIN 22
#define BOARD_SD_MOSI_PIN 23

// --- PSRAM ---
//
// Fitted on some Feathers (CS GP8, not on the headers). Not used yet; the
// firmware keeps it deselected, which is harmless where it is missing.
#define BOARD_PSRAM_CS_PIN 8

#endif
