// Frankless: a reworked MicroFRANK (github.com/arlaneenalra/pi-pico-boards,
// reworked/frankless; after github.com/rh1tech/frank, hardware/microfrank).
// RP2350A, 16 MB flash, 8 MB PSRAM, HDMI connector on the HSTX pins, micro SD
// slot, a CH334R hub to two USB-A ports, DS3231M RTC. Pins taken from the
// KiCad schematic and PCB at commit 8edc485; partly tried on the board
// (bring-up in bd 59u). The Z80 machine's banks are in the PSRAM
// (PSRAM_BANKS: 15 x 48 KB), the clock is the RTC's. boards/example.h
// explains each setting.
//
// The board's other pins, which the firmware leaves alone (inputs with the
// reset pulls): GP24-29 on header J1. No RGB LED, card detect, user button
// or battery (the RTC's cell aside).
//
// USB: slide switch S1 routes the RP2350's USB either to the USB-C socket
// (flashing over BOOTSEL) or to the hub (keyboards and sticks); the hub
// ports take 5 V straight from the USB-C input, so no enable pin.

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FRANKLESS_H
#define _BOARDS_FRANKLESS_H

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

// A 12 MHz CMOS oscillator (Y1) drives XIN, XOUT open: the RP2350's way of
// taking an external clock, as on the MicroFRANK; the SDK's XOSC setup is
// unchanged.

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- DVI (HSTX, GP12-19) ---
//
// Through 270R arrays (RN1, RN2) to the HDMI connector; - on the even pin of
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
// UART1 on header J8: 3V3, GND, RTS, CTS, RX, TX (pins 1-6).
#define BOARD_UART 1
#define BOARD_UART_TX_PIN 20
#define BOARD_UART_RX_PIN 21
#define BOARD_UART_CTS_PIN 22 // SERIAL_FLOW_CONTROL builds
#define BOARD_UART_RTS_PIN 23

#define PICO_DEFAULT_UART BOARD_UART
#define PICO_DEFAULT_UART_TX_PIN BOARD_UART_TX_PIN
#define PICO_DEFAULT_UART_RX_PIN BOARD_UART_RX_PIN

// --- SD card (SPI0) ---
//
// 10k pull-ups on all six card lines; card detect not connected.
#define BOARD_SD_SPI 0
#define BOARD_SD_MISO_PIN 4
#define BOARD_SD_CS_PIN 1
#define BOARD_SD_SCK_PIN 2
#define BOARD_SD_MOSI_PIN 3

// --- PSRAM ---
//
// APS6404L (8 MB) on the QSPI bus, CE on GP0 (XIP_CS1) with a 10k pull-up.
// The SDK's PSRAM setup (runtime init) checks that it answers and its size.
#define BOARD_PSRAM_CS_PIN 0
#define PICO_PSRAM_CS_PIN BOARD_PSRAM_CS_PIN
#define PICO_AUTO_DETECT_PSRAM_SIZE 1

// --- Real-time clock ---
//
// DS3231M on I2C1 with 4.7k pull-ups; VBAT from an external battery holder
// (without a cell the time is lost at power-off). Its 1 Hz SQW output on
// GP8 (open drain, 4.7k pull-up) keeps the firmware's clock on the chip's.
#define BOARD_RTC_DS3231 1
#define BOARD_RTC_I2C 1
#define BOARD_RTC_SDA_PIN 10
#define BOARD_RTC_SCL_PIN 11
#define BOARD_RTC_SQW_PIN 8

// --- Audio ---
//
// U8, a TDA1387 stereo DAC (hand-fitted: not in the JLCPCB assembly),
// 16-bit words left-justified (at 16 bits, the same as its LSB-justified
// format), to the audio amplifier and jack: the bell.
#define BOARD_AUDIO_I2S_BCK_PIN 5
#define BOARD_AUDIO_I2S_WS_PIN 6
#define BOARD_AUDIO_I2S_DATA_PIN 7
#define BOARD_AUDIO_I2S_NAME "TDA1387"

// --- LED ---
//
// LD2, through 1k to GND.
#define PICO_DEFAULT_LED_PIN 9

#endif
