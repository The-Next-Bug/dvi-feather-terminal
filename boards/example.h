// Template for a new board: copy it to boards/<target>.h, edit it, and build
// with TARGET=<target> scripts/build.sh. As it stands it describes the same
// wiring as the Pico 2 with the DVI Sock, so it builds and runs there.
//
// The file is a pico-sdk board header (the SDK's CMake finds it through
// PICO_BOARD_HEADER_DIRS and reads its pico_board_cmake_set lines) with the
// terminal's pins added. src/board/board.h checks the pins at compile time: wrong
// GPIOs for a peripheral, a pin used twice, or a missing setting stop the
// build with an #error that names the setting. Give pins as plain decimal
// numbers: some are also shown on screen (e.g. "none found (SPI0: GP4 ...").
//
// A board built on a Pico 2 module can include "boards/pico2.h" instead of
// the RP2350/flash/crystal section below (see boards/pico2-dvi-sock.h).

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_EXAMPLE_H
#define _BOARDS_EXAMPLE_H

// --- RP2350, flash, crystal ---

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// RP2350A (QFN-60, GPIO 0-29) is 1; RP2350B (QFN-80, GPIO 0-47) is 0.
#define PICO_RP2350A 1

// The QSPI flash chip. The W25Q080 boot stage 2 suits most Winbond-like
// parts; the firmware switches flash reads to the plain 03h command after
// raising the clock anyway (src/platform/nvflash.c).
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// Flash size. The firmware puts the update staging area at half of it and
// the settings at the end (src/platform/fwupdate.h, src/platform/nvflash.h), so the largest
// firmware image is a little under half the flash: about 1 MB with 2 MB of
// flash, against an image of about 0.5 MB today.
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (4 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif

// The crystal. Only 12 MHz has been tried: the DVI clocks (clk_sys 400 MHz
// for 800x600@60) come from the PLL, which needs a reachable multiple.
// Slow-starting crystals want a larger startup delay (the SDK default is 1).
// #define XOSC_HZ 12000000
// #define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- DVI (HSTX) ---
//
// The HSTX drives GP12-19 only, so the four TMDS pairs must use exactly
// those eight pins, but in any order and either way round. D0 carries blue
// and the syncs, D1 green, D2 red (the DVI connector's TMDS Data0/1/2).
//
// Each pin is set separately: a pair whose traces are swapped (+ routed to
// the connector's - pin) is fixed here by swapping its P and N numbers, and
// the two pins of a pair need not be neighbors. For example, the clock on
// GP19/GP18 with + and - crossed is CLK_P 18, CLK_N 19.
#define BOARD_DVI_D0_P_PIN 12
#define BOARD_DVI_D0_N_PIN 13
#define BOARD_DVI_CLK_P_PIN 14
#define BOARD_DVI_CLK_N_PIN 15
#define BOARD_DVI_D2_P_PIN 16
#define BOARD_DVI_D2_N_PIN 17
#define BOARD_DVI_D1_P_PIN 18
#define BOARD_DVI_D1_N_PIN 19

// Pad drive strength for the eight DVI pins, in mA (2, 4, 8 or 12), and the
// slew rate (0 limited, 1 fast). The defaults, 2 mA and limited, are what
// PicoDVI uses and work through the DVI Sock's series resistors; a board
// with longer traces or a level shifter may want more.
// #define BOARD_DVI_DRIVE_MA 2
// #define BOARD_DVI_SLEW_FAST 0

// --- Terminal UART ---
//
// The serial line to the host (and, in VIDEO_DIAG builds, the diagnostics
// on its TX): UART 0 or 1, and its pins. On the RP2350 GPIO n belongs to
// UART ((n + 4) >> 3) & 1 (GP0-3 UART0, GP4-11 UART1, GP12-19 UART0,
// GP20-27 UART1, ...), as n % 4 = 0 TX, 1 RX, 2 CTS, 3 RTS; n % 4 = 2 and
// 3 can also be TX and RX (the auxiliary function).
//
// CTS (in) and RTS (out) are used by SERIAL_FLOW_CONTROL builds only; leave
// them out if the board has no handshake lines.
#define BOARD_UART 0
#define BOARD_UART_TX_PIN 0
#define BOARD_UART_RX_PIN 1
#define BOARD_UART_CTS_PIN 2
#define BOARD_UART_RTS_PIN 3

// The SDK's default UART is the same one, so stdio in test programs
// (hstx_test) comes out on these pins. (A header that includes one of the
// SDK's boards sets these first, as the SDK's only fill in what is unset.)
#define PICO_DEFAULT_UART BOARD_UART
#define PICO_DEFAULT_UART_TX_PIN BOARD_UART_TX_PIN
#define PICO_DEFAULT_UART_RX_PIN BOARD_UART_RX_PIN

// --- SD card (SPI, for the CP/M machine and firmware updates) ---
//
// GPIO n is SPI (n >> 3) & 1, with n % 4 = 0 RX (card DO), 2 SCK, 3 TX
// (card DI). CS is driven as a plain GPIO: any free pin.
#define BOARD_SD_SPI 0
#define BOARD_SD_MISO_PIN 4
#define BOARD_SD_CS_PIN 5
#define BOARD_SD_SCK_PIN 6
#define BOARD_SD_MOSI_PIN 7

// --- USB host ---
//
// The keyboard and USB sticks hang off the RP2350's own USB port (no pins
// to set). The RP2350 cannot power VBUS itself: a board with a load switch
// for the 5 V names its enable pin here and the firmware turns it on before
// starting the USB host; LEVEL is what turns it on.
// #define BOARD_USB_VBUS_EN_PIN 22
// #define BOARD_USB_VBUS_EN_LEVEL 1

// --- Flash ---
//
// The firmware's cold code runs from flash: in quad (EBh) mode if all four
// QSPI data lines reach the flash and it reads back the same as in serial
// (03h) mode, else serial. Set 0 to always stay serial.
// #define BOARD_FLASH_QUAD 1

// --- PSRAM (optional) ---
//
// A QSPI PSRAM sharing the flash's bus, on the QMI's second chip select
// (XIP_CS1: GP0, GP8, GP19 or GP47). To give it the Z80 machine's banks
// (up to 15 x 48 KB; none in SRAM), also tell CMake and the SDK:
//   pico_board_cmake_set(PSRAM_BANKS, 1)
//   #define PICO_PSRAM_CS_PIN BOARD_PSRAM_CS_PIN
//   #define PICO_AUTO_DETECT_PSRAM_SIZE 1  // checks it answers
// (boards/frankless.h). Without PSRAM_BANKS the firmware only drives this
// pin high at start-up so the PSRAM stays off the bus.
// #define BOARD_PSRAM_CS_PIN 8

// --- Real-time clock (optional) ---
//
// One chip at I2C address 68h: BOARD_RTC_DS3231 (DS3231 or DS3231M) or
// BOARD_RTC_PCF8523 (the Adalogger FeatherWing's; less exact, about
// +-2 s/day; years to 2099), on BOARD_RTC_I2C (0/1) and its pins
// (GPIO n: I2C (n >> 1) & 1, SDA on even, SCL on odd pins); the board's
// pull-ups (the internal ones are on too). The time is read at start-up and
// kept by the chip's battery; CP/M 3's DATE SET and SET-UP's Clock panel set
// it. BOARD_RTC_SQW_PIN, the chip's 1 Hz output, keeps the firmware's clock
// on the chip's seconds (DS3231 only, so far).
// #define BOARD_RTC_DS3231 1
// #define BOARD_RTC_PCF8523 1
// #define BOARD_RTC_I2C 1
// #define BOARD_RTC_SDA_PIN 10
// #define BOARD_RTC_SCL_PIN 11
// #define BOARD_RTC_SQW_PIN 8

// --- Other optional hardware ---
//
// Name what the board has, even before the firmware drives it: HWINFO lists
// it (as planned until its driver lands) and leaves out what is not named.
//
// An I2S DAC (or one taking 16-bit left-justified words, as the TDA1387):
// the bell, a short beep on BEL. WS must be BCK + 1 (PIO side-set pins);
// the name is only for HWINFO.
// #define BOARD_AUDIO_I2S_BCK_PIN 9
// #define BOARD_AUDIO_I2S_WS_PIN 10
// #define BOARD_AUDIO_I2S_DATA_PIN 11
// #define BOARD_AUDIO_I2S_NAME "TDA1387"
//
// Planned (no driver yet): a buzzer, an RGB status LED (WS2812), the SD
// slot's card detect switch, a user button, the battery's voltage (an ADC
// pin: GP26-29) and the charger's status.
// #define BOARD_BUZZER_PIN 10
// #define BOARD_NEOPIXEL_PIN 21
// #define BOARD_SD_CD_PIN 22
// #define BOARD_BUTTON_PIN 24
// #define BOARD_BATTERY_ADC_PIN 26
// #define BOARD_CHARGER_STAT_PIN 27

// --- LED ---
//
// Flashes on SD card, USB stick and serial activity, and once a second
// while the screen is blanked (src/platform/led.c); VIDEO_DIAG builds and hstx_test
// blink it their own way. Leave it out if the board has none.
#define PICO_DEFAULT_LED_PIN 25

#endif
