// Template for a new board: copy it to boards/<target>.h, edit it, and build
// with TARGET=<target> scripts/build.sh. As it stands it describes the same
// wiring as the Pico 2 with the DVI Sock, so it builds and runs there.
//
// The file is a pico-sdk board header (the SDK's CMake finds it through
// PICO_BOARD_HEADER_DIRS and reads its pico_board_cmake_set lines) with the
// terminal's pins added. src/board.h checks the pins at compile time: wrong
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
// raising the clock anyway (src/nvflash.c).
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// Flash size. The firmware puts the update staging area at half of it and
// the settings at the end (src/fwupdate.h, src/nvflash.h), so the largest
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
// the two pins of a pair need not be neighbours. For example, the clock on
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

// --- LED ---
//
// Lit/blinked by VIDEO_DIAG builds and hstx_test only. Leave it out if the
// board has none.
#define PICO_DEFAULT_LED_PIN 25

#endif
