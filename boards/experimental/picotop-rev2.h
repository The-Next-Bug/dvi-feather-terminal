// 7West PicoTop, board Rev2 (github.com/7west/PicoTop, HARDWARE/PicoTop
// Board Rev2): RP2350A, 16 MB flash, HDMI connector on the HSTX pins, a
// micro SD slot, one USB-A host port and a LiPo charger. Pins taken from the
// KiCad netlist. boards/example.h explains each setting.
//
// EXPERIMENTAL: never run on the board, and no board is expected here; CI
// does not build it. Build with EXPERIMENTAL=1 TARGET=picotop-rev2
// scripts/build.sh (boards/experimental/README.md).
//
// The board's other pins, which the firmware leaves alone (inputs with the
// reset pulls; no drivers for them, bd 1ls.22): GP2/3 the charger's
// STAT2/STAT1 (open drain, LEDs to 3V3), GP6/7 I2C1 to the RV-3028 RTC,
// GP8/9 on the header, GP10 the buzzer (high = on), GP11 a switch to GND,
// GP24 the SD card detect, GP28 the battery voltage (ADC2). GP4/5 and
// GP25-27 are not routed.
//
// No RTS/CTS (Flow control: XON/XOFF only): UART0's CTS/RTS pins (GP2/3)
// carry the charger status, and the header has only TX/RX.
//
// Flashing: the USB-C socket is power only and the USB-A port is a host
// port, so the first image goes in over SWD (header J1 pins 1/2); later ones
// through SET-UP's U.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_PICOTOP_REV2_H
#define _BOARDS_PICOTOP_REV2_H

// --- RP2350, flash, crystal ---

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PICO_RP2350A 1

// W25Q128JV.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// ABM8-272-T3, 12 MHz, with a 1k series resistor as on the Pico 2.

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- DVI (HSTX, GP12-19) ---
//
// Through 220R arrays (RN1, RN2) to the HDMI connector; - on the even pin of
// each pair. HPD and DDC are not connected.
#define BOARD_DVI_CLK_P_PIN 13
#define BOARD_DVI_CLK_N_PIN 12
#define BOARD_DVI_D0_P_PIN 15
#define BOARD_DVI_D0_N_PIN 14
#define BOARD_DVI_D1_P_PIN 17
#define BOARD_DVI_D1_N_PIN 16
#define BOARD_DVI_D2_P_PIN 19
#define BOARD_DVI_D2_N_PIN 18

// --- Terminal UART ---
//
// The header's DBG_TX/DBG_RX (J1 pins 6 and 5).
#define BOARD_UART 0
#define BOARD_UART_TX_PIN 0
#define BOARD_UART_RX_PIN 1

#define PICO_DEFAULT_UART BOARD_UART
#define PICO_DEFAULT_UART_TX_PIN BOARD_UART_TX_PIN
#define PICO_DEFAULT_UART_RX_PIN BOARD_UART_RX_PIN

// --- SD card (SPI0) ---
//
// CS has a 10k pull-up on the board.
#define BOARD_SD_SPI 0
#define BOARD_SD_MISO_PIN 20
#define BOARD_SD_CS_PIN 21
#define BOARD_SD_SCK_PIN 22
#define BOARD_SD_MOSI_PIN 23

// --- USB host ---
//
// GP29 high turns on the 5 V boost converter (TPS61092, from the battery /
// charger rail) that feeds the USB-A port, and also the HDMI connector's
// +5V pin, so main() turns it on before the video starts (bd gy1).
#define BOARD_USB_VBUS_EN_PIN 29
#define BOARD_USB_VBUS_EN_LEVEL 1

// --- LED ---
//
// None on a GPIO (the two LEDs are the charger's).

#endif
