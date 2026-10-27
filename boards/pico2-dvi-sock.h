// Raspberry Pi Pico 2 (RP2350A, 4 MB flash) with the Pico DVI Sock: the
// default target. boards/example.h explains each setting.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_PICO2_DVI_SOCK_H
#define _BOARDS_PICO2_DVI_SOCK_H

// --- Terminal UART ---
#define BOARD_UART 0
#define BOARD_UART_TX_PIN 0
#define BOARD_UART_RX_PIN 1
#define BOARD_UART_CTS_PIN 2 // used with the Flow control setting's RTS/CTS
#define BOARD_UART_RTS_PIN 3
// Before pico2.h, which only sets what is unset.
#define PICO_DEFAULT_UART BOARD_UART
#define PICO_DEFAULT_UART_TX_PIN BOARD_UART_TX_PIN
#define PICO_DEFAULT_UART_RX_PIN BOARD_UART_RX_PIN

// The Pico 2's own settings: RP2350A, flash, crystal and the LED on GP25.
#include "boards/pico2.h"

// --- DVI (HSTX, GP12-19) ---
#define BOARD_DVI_D0_P_PIN 12
#define BOARD_DVI_D0_N_PIN 13
#define BOARD_DVI_CLK_P_PIN 14
#define BOARD_DVI_CLK_N_PIN 15
#define BOARD_DVI_D2_P_PIN 16
#define BOARD_DVI_D2_N_PIN 17
#define BOARD_DVI_D1_P_PIN 18
#define BOARD_DVI_D1_N_PIN 19

// --- SD card ---
#define BOARD_SD_SPI 0
#define BOARD_SD_MISO_PIN 4
#define BOARD_SD_CS_PIN 5
#define BOARD_SD_SCK_PIN 6
#define BOARD_SD_MOSI_PIN 7

#endif
