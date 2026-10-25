// Stock MicroFRANK (github.com/rh1tech/frank, hardware/microfrank; its "M2"
// GPIO layout): RP2350A, 16 MB flash, 8 MB PSRAM, HDMI connector on the HSTX
// pins, micro SD slot, a hub to two USB-A ports, TDA1387 DAC. Pins from the
// KiCad schematics, cross-checked with frank-kickstart's src/board_config.h
// (BOARD_M2); fit analysis in bd 59u. boards/frankless.h is the user's
// rework of this board. boards/example.h explains each setting.
//
// EXPERIMENTAL: never run on the board, and no board is expected here; CI
// does not build it. Build with EXPERIMENTAL=1 TARGET=microfrank
// scripts/build.sh (boards/experimental/README.md).
//
// The board's other pins: GP2/3, GP20-24 and GP26-29 are not broken out
// (only the SWD, power and UART headers are). No RTC, card detect, user
// button or battery.
//
// No SERIAL_FLOW_CONTROL: the UART header has only TX and RX.
//
// USB: slide switch S1 (a TS3USB221 mux) routes the RP2350's USB either to
// the USB-C socket (flashing over BOOTSEL) or to the hub; the hub ports take
// 5 V straight from the board's VBUS, so no enable pin.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_MICROFRANK_H
#define _BOARDS_MICROFRANK_H

// --- RP2350, flash, crystal ---

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PSRAM_BANKS, 1)

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

// A 12 MHz CMOS oscillator drives XIN, XOUT open: the RP2350's way of
// taking an external clock; the SDK's XOSC setup is unchanged.

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- DVI (HSTX, GP12-19) ---
//
// - on the even pin of each pair, as on the Frankless. HPD and DDC are not
// connected.
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
// UART0 on the 3-pin J9 "DEBUG" header: GP0, GND, GP1 (3.3 V).
#define BOARD_UART 0
#define BOARD_UART_TX_PIN 0
#define BOARD_UART_RX_PIN 1

#define PICO_DEFAULT_UART BOARD_UART
#define PICO_DEFAULT_UART_TX_PIN BOARD_UART_TX_PIN
#define PICO_DEFAULT_UART_RX_PIN BOARD_UART_RX_PIN

// --- SD card (SPI0) ---
//
// The same pins as the DVI Sock's: DAT0 4, DAT3 5, CLK 6, CMD 7.
#define BOARD_SD_SPI 0
#define BOARD_SD_MISO_PIN 4
#define BOARD_SD_CS_PIN 5
#define BOARD_SD_SCK_PIN 6
#define BOARD_SD_MOSI_PIN 7

// --- PSRAM ---
//
// ESP-PSRAM64H (8 MB, APS6404L-compatible) on the QSPI bus, CE on GP8
// (XIP_CS1) with a 10k pull-up. The SDK's PSRAM setup (runtime init) checks
// that it answers and its size.
#define BOARD_PSRAM_CS_PIN 8
#define PICO_PSRAM_CS_PIN BOARD_PSRAM_CS_PIN
#define PICO_AUTO_DETECT_PSRAM_SIZE 1

// --- Audio ---
//
// TDA1387 stereo DAC: frank-kickstart's I2S_DATA_PIN 9 and
// I2S_CLOCK_PIN_BASE 10 (BCK 10, LRCK 11), 16-bit words left-justified: the
// bell.
#define BOARD_AUDIO_I2S_BCK_PIN 10
#define BOARD_AUDIO_I2S_WS_PIN 11
#define BOARD_AUDIO_I2S_DATA_PIN 9
#define BOARD_AUDIO_I2S_NAME "TDA1387"

// --- LED ---
#define PICO_DEFAULT_LED_PIN 25

#endif
