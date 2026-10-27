#ifndef BOARD_H
#define BOARD_H

// The board's pins, from boards/<PICO_BOARD>.h (boards/example.h documents
// each setting), checked here at compile time, plus what the firmware
// derives from them. RP2350 only: portable modules must not include it.

#include "pico.h"

#if !defined(BOARD_DVI_CLK_P_PIN) || !defined(BOARD_DVI_CLK_N_PIN) || !defined(BOARD_DVI_D0_P_PIN) ||   \
	!defined(BOARD_DVI_D0_N_PIN) || !defined(BOARD_DVI_D1_P_PIN) || !defined(BOARD_DVI_D1_N_PIN) || \
	!defined(BOARD_DVI_D2_P_PIN) || !defined(BOARD_DVI_D2_N_PIN)
#error "the board header must set every BOARD_DVI_*_PIN (see boards/example.h)"
#endif
#if !defined(BOARD_UART) || !defined(BOARD_UART_TX_PIN) || !defined(BOARD_UART_RX_PIN)
#error "the board header must set BOARD_UART, BOARD_UART_TX_PIN and BOARD_UART_RX_PIN (see boards/example.h)"
#endif
#if !defined(BOARD_SD_SPI) || !defined(BOARD_SD_MISO_PIN) || !defined(BOARD_SD_CS_PIN) || \
	!defined(BOARD_SD_SCK_PIN) || !defined(BOARD_SD_MOSI_PIN)
#error "the board header must set BOARD_SD_SPI and every BOARD_SD_*_PIN (see boards/example.h)"
#endif

// --- DVI ---

// The HSTX drives GP12-19; bit n of its output is GP12 + n.
#define BOARD_HSTX_FIRST_PIN 12
#define BOARD_PIN_BIT(pin) (1ull << (pin))
#if (BOARD_PIN_BIT(BOARD_DVI_CLK_P_PIN) | BOARD_PIN_BIT(BOARD_DVI_CLK_N_PIN) |                  \
	BOARD_PIN_BIT(BOARD_DVI_D0_P_PIN) | BOARD_PIN_BIT(BOARD_DVI_D0_N_PIN) |                 \
	BOARD_PIN_BIT(BOARD_DVI_D1_P_PIN) | BOARD_PIN_BIT(BOARD_DVI_D1_N_PIN) |                 \
	BOARD_PIN_BIT(BOARD_DVI_D2_P_PIN) | BOARD_PIN_BIT(BOARD_DVI_D2_N_PIN)) != 0xff000ull
#error "the BOARD_DVI_*_PIN must use each of GP12-19 (the HSTX pins) once"
#endif

#ifndef BOARD_DVI_DRIVE_MA
#define BOARD_DVI_DRIVE_MA 2
#endif

// Flash XIP reads in quad (EBh) mode (nvflash.c, bd cpx.2): 1 tries it and
// keeps it if it reads the same as serial 03h; 0 stays serial.
#ifndef BOARD_FLASH_QUAD
#define BOARD_FLASH_QUAD 1
#endif
// PADS_BANK0 DRIVE field: 0 2 mA, 1 4 mA, 2 8 mA, 3 12 mA.
#if BOARD_DVI_DRIVE_MA == 2
#define BOARD_DVI_DRIVE 0u
#elif BOARD_DVI_DRIVE_MA == 4
#define BOARD_DVI_DRIVE 1u
#elif BOARD_DVI_DRIVE_MA == 8
#define BOARD_DVI_DRIVE 2u
#elif BOARD_DVI_DRIVE_MA == 12
#define BOARD_DVI_DRIVE 3u
#else
#error "BOARD_DVI_DRIVE_MA must be 2, 4, 8 or 12"
#endif
#ifndef BOARD_DVI_SLEW_FAST
#define BOARD_DVI_SLEW_FAST 0
#endif

// --- Terminal UART ---

// The RP2350's function map: GPIO n is UART ((n + 4) >> 3) & 1 (0-3 UART0,
// 4-11 UART1, 12-19 UART0, ...) with n % 4 = 0 TX, 1 RX, 2 CTS, 3 RTS; the
// auxiliary function gives TX on n % 4 = 2 and RX on 3.
#define BOARD_UART_OF(pin) ((((pin) + 4) >> 3) & 1)
#if BOARD_UART != 0 && BOARD_UART != 1
#error "BOARD_UART must be 0 or 1"
#endif
#if BOARD_UART_OF(BOARD_UART_TX_PIN) != BOARD_UART || BOARD_UART_TX_PIN % 2 != 0
#error "BOARD_UART_TX_PIN is not a TX pin of BOARD_UART"
#endif
#if BOARD_UART_OF(BOARD_UART_RX_PIN) != BOARD_UART || BOARD_UART_RX_PIN % 2 != 1
#error "BOARD_UART_RX_PIN is not an RX pin of BOARD_UART"
#endif
#ifdef BOARD_UART_CTS_PIN
#if BOARD_UART_OF(BOARD_UART_CTS_PIN) != BOARD_UART || BOARD_UART_CTS_PIN % 4 != 2
#error "BOARD_UART_CTS_PIN is not a CTS pin of BOARD_UART"
#endif
#endif
#ifdef BOARD_UART_RTS_PIN
#if BOARD_UART_OF(BOARD_UART_RTS_PIN) != BOARD_UART || BOARD_UART_RTS_PIN % 4 != 3
#error "BOARD_UART_RTS_PIN is not an RTS pin of BOARD_UART"
#endif
#endif
// The SDK's default UART (test programs' stdio) should be the same one; a
// board header that includes an SDK board after it sets its own is caught.
#if defined(PICO_DEFAULT_UART) && (PICO_DEFAULT_UART != BOARD_UART || \
	PICO_DEFAULT_UART_TX_PIN != BOARD_UART_TX_PIN || PICO_DEFAULT_UART_RX_PIN != BOARD_UART_RX_PIN)
#error "PICO_DEFAULT_UART and its pins differ from BOARD_UART's: set them to BOARD_UART* before including an SDK board"
#endif
#define BOARD_UART_INST UART_INSTANCE(BOARD_UART)

// --- SD card ---

// GPIO n is SPI (n >> 3) & 1 with n % 4 = 0 RX, 1 CSn, 2 SCK, 3 TX. CS is
// a plain GPIO here.
#define BOARD_SPI_OF(pin) (((pin) >> 3) & 1)
#if BOARD_SPI_OF(BOARD_SD_MISO_PIN) != BOARD_SD_SPI || BOARD_SD_MISO_PIN % 4 != 0
#error "BOARD_SD_MISO_PIN is not an RX pin of BOARD_SD_SPI"
#endif
#if BOARD_SPI_OF(BOARD_SD_SCK_PIN) != BOARD_SD_SPI || BOARD_SD_SCK_PIN % 4 != 2
#error "BOARD_SD_SCK_PIN is not an SCK pin of BOARD_SD_SPI"
#endif
#if BOARD_SPI_OF(BOARD_SD_MOSI_PIN) != BOARD_SD_SPI || BOARD_SD_MOSI_PIN % 4 != 3
#error "BOARD_SD_MOSI_PIN is not a TX pin of BOARD_SD_SPI"
#endif
#define BOARD_SD_SPI_INST SPI_INSTANCE(BOARD_SD_SPI)

// --- PSRAM (optional) ---

// The QMI's second chip select (XIP_CS1) is on GP0, GP8, GP19 or GP47 only.
#if defined(BOARD_PSRAM_CS_PIN) && BOARD_PSRAM_CS_PIN != 0 && BOARD_PSRAM_CS_PIN != 8 && \
	BOARD_PSRAM_CS_PIN != 19 && BOARD_PSRAM_CS_PIN != 47
#error "BOARD_PSRAM_CS_PIN is not an XIP_CS1 pin (GP0, GP8, GP19, GP47)"
#endif

// --- Real-time clock (optional) ---

// GPIO n is I2C (n >> 1) & 1 with SDA on even pins, SCL on odd ones.
#define BOARD_I2C_OF(pin) (((pin) >> 1) & 1)
// The chip: one of BOARD_RTC_DS3231, BOARD_RTC_PCF8523 (src/rtc/).
#if defined(BOARD_RTC_DS3231) && defined(BOARD_RTC_PCF8523)
#error "the board header names two RTC chips: BOARD_RTC_DS3231 and BOARD_RTC_PCF8523"
#endif
#if defined(BOARD_RTC_DS3231) || defined(BOARD_RTC_PCF8523)
#define BOARD_HAS_RTC 1
#endif
#if defined(BOARD_RTC_PCF8523) && defined(BOARD_RTC_SQW_PIN)
#error "BOARD_RTC_SQW_PIN: the PCF8523's 1 Hz output is not supported yet"
#endif
#ifdef BOARD_HAS_RTC
#if !defined(BOARD_RTC_I2C) || !defined(BOARD_RTC_SDA_PIN) || !defined(BOARD_RTC_SCL_PIN)
#error "the RTC chip needs BOARD_RTC_I2C, BOARD_RTC_SDA_PIN and BOARD_RTC_SCL_PIN (see boards/example.h)"
#endif
#if BOARD_RTC_I2C != 0 && BOARD_RTC_I2C != 1
#error "BOARD_RTC_I2C must be 0 or 1"
#endif
#if BOARD_I2C_OF(BOARD_RTC_SDA_PIN) != BOARD_RTC_I2C || BOARD_RTC_SDA_PIN % 2 != 0
#error "BOARD_RTC_SDA_PIN is not an SDA pin of BOARD_RTC_I2C"
#endif
#if BOARD_I2C_OF(BOARD_RTC_SCL_PIN) != BOARD_RTC_I2C || BOARD_RTC_SCL_PIN % 2 != 1
#error "BOARD_RTC_SCL_PIN is not an SCL pin of BOARD_RTC_I2C"
#endif
#define BOARD_RTC_I2C_INST I2C_INSTANCE(BOARD_RTC_I2C)
#define BOARD_RTC_BITS (BOARD_PIN_BIT(BOARD_RTC_SDA_PIN) | BOARD_PIN_BIT(BOARD_RTC_SCL_PIN))
#elif defined(BOARD_RTC_I2C) || defined(BOARD_RTC_SDA_PIN) || defined(BOARD_RTC_SCL_PIN) || defined(BOARD_RTC_SQW_PIN)
#error "BOARD_RTC_I2C and its pins need a chip: BOARD_RTC_DS3231 or BOARD_RTC_PCF8523"
#else
#define BOARD_RTC_BITS 0ull
#endif
#ifdef BOARD_RTC_SQW_PIN
#define BOARD_SQW_BIT BOARD_PIN_BIT(BOARD_RTC_SQW_PIN)
#else
#define BOARD_SQW_BIT 0ull
#endif

// --- Other optional hardware (bd 1ls.4) ---
//
// A board header names what it has, also before the firmware drives it:
// HWINFO reports such hardware as planned, and leaves out what the board
// does not name.

// An I2S (or 16-bit left-justified) DAC: the bell (bell.c). BCK and WS are
// PIO side-set pins, so WS is BCK + 1.
#if defined(BOARD_AUDIO_I2S_BCK_PIN) || defined(BOARD_AUDIO_I2S_WS_PIN) || defined(BOARD_AUDIO_I2S_DATA_PIN)
#if !defined(BOARD_AUDIO_I2S_BCK_PIN) || !defined(BOARD_AUDIO_I2S_WS_PIN) || !defined(BOARD_AUDIO_I2S_DATA_PIN)
#error "an I2S DAC needs BOARD_AUDIO_I2S_BCK_PIN, BOARD_AUDIO_I2S_WS_PIN and BOARD_AUDIO_I2S_DATA_PIN"
#endif
#if BOARD_AUDIO_I2S_WS_PIN != BOARD_AUDIO_I2S_BCK_PIN + 1
#error "BOARD_AUDIO_I2S_WS_PIN must be BOARD_AUDIO_I2S_BCK_PIN + 1 (PIO side-set pins)"
#endif
#define BOARD_AUDIO_I2S 1
#define BOARD_I2S_BITS (BOARD_PIN_BIT(BOARD_AUDIO_I2S_BCK_PIN) | BOARD_PIN_BIT(BOARD_AUDIO_I2S_WS_PIN) | \
	BOARD_PIN_BIT(BOARD_AUDIO_I2S_DATA_PIN))
#ifndef BOARD_AUDIO_I2S_NAME
#define BOARD_AUDIO_I2S_NAME "I2S DAC"
#endif
#else
#define BOARD_AUDIO_I2S 0
#define BOARD_I2S_BITS 0ull
#endif

// Dropped hardware (bd 1ls.22): a header that still names it would
// otherwise build as if the pin were free.
#if defined(BOARD_BUZZER_PIN) || defined(BOARD_SD_CD_PIN) || defined(BOARD_BUTTON_PIN) || \
	defined(BOARD_BATTERY_ADC_PIN) || defined(BOARD_CHARGER_STAT_PIN)
#error "BOARD_BUZZER_PIN, BOARD_SD_CD_PIN, BOARD_BUTTON_PIN, BOARD_BATTERY_ADC_PIN and BOARD_CHARGER_STAT_PIN were dropped: remove them"
#endif

// An RGB LED (WS2812).
#ifdef BOARD_NEOPIXEL_PIN
#define BOARD_NEOPIXEL_BIT BOARD_PIN_BIT(BOARD_NEOPIXEL_PIN)
#else
#define BOARD_NEOPIXEL_BIT 0ull
#endif

// --- Every pin used once ---

// Pins that are optional or only used in some builds count as used when
// set; with all distinct, the sum of their bits equals their OR.
#ifdef BOARD_UART_CTS_PIN
#define BOARD_CTS_BIT BOARD_PIN_BIT(BOARD_UART_CTS_PIN)
#else
#define BOARD_CTS_BIT 0ull
#endif
#ifdef BOARD_UART_RTS_PIN
#define BOARD_RTS_BIT BOARD_PIN_BIT(BOARD_UART_RTS_PIN)
#else
#define BOARD_RTS_BIT 0ull
#endif
#ifdef BOARD_USB_VBUS_EN_PIN
#define BOARD_VBUS_BIT BOARD_PIN_BIT(BOARD_USB_VBUS_EN_PIN)
#ifndef BOARD_USB_VBUS_EN_LEVEL
#define BOARD_USB_VBUS_EN_LEVEL 1
#endif
#else
#define BOARD_VBUS_BIT 0ull
#endif
#ifdef PICO_DEFAULT_LED_PIN
#define BOARD_LED_BIT BOARD_PIN_BIT(PICO_DEFAULT_LED_PIN)
#else
#define BOARD_LED_BIT 0ull
#endif
#ifdef BOARD_PSRAM_CS_PIN
#define BOARD_PSRAM_BIT BOARD_PIN_BIT(BOARD_PSRAM_CS_PIN)
#else
#define BOARD_PSRAM_BIT 0ull
#endif
#define BOARD_OTHER_PINS(op)                                                                       \
	(BOARD_PIN_BIT(BOARD_UART_TX_PIN) op BOARD_PIN_BIT(BOARD_UART_RX_PIN) op           \
	BOARD_CTS_BIT op BOARD_RTS_BIT op BOARD_PIN_BIT(BOARD_SD_MISO_PIN) op                           \
	BOARD_PIN_BIT(BOARD_SD_CS_PIN) op BOARD_PIN_BIT(BOARD_SD_SCK_PIN) op                            \
	BOARD_PIN_BIT(BOARD_SD_MOSI_PIN) op BOARD_VBUS_BIT op BOARD_LED_BIT op BOARD_PSRAM_BIT op       \
	BOARD_RTC_BITS op BOARD_SQW_BIT op BOARD_I2S_BITS op BOARD_NEOPIXEL_BIT)
#if BOARD_OTHER_PINS(+) != BOARD_OTHER_PINS(|)
#error "the board header uses a pin twice (UART, SD card, USB VBUS enable, LED, PSRAM, RTC, other hardware)"
#endif
#if BOARD_OTHER_PINS(|) & 0xff000ull
#error "the board header gives a DVI pin (GP12-19) another job"
#endif
#if BOARD_OTHER_PINS(|) >> NUM_BANK0_GPIOS
#error "the board header uses a GPIO this RP2350 does not have"
#endif

// --- Pin names for messages ---

#define BOARD_STR_(x) #x
#define BOARD_STR(x) BOARD_STR_(x)
#define BOARD_SD_PINS_TEXT                                                                         \
	"SPI" BOARD_STR(BOARD_SD_SPI) ": GP" BOARD_STR(BOARD_SD_MISO_PIN) " MISO, GP" BOARD_STR(          \
		BOARD_SD_CS_PIN) " CS, GP" BOARD_STR(BOARD_SD_SCK_PIN) " SCK, GP" BOARD_STR(BOARD_SD_MOSI_PIN) " MOSI"
#define BOARD_UART_NAME "UART" BOARD_STR(BOARD_UART)
#define BOARD_UART_PINS_TEXT                                                                       \
	BOARD_UART_NAME " on TX (GP" BOARD_STR(BOARD_UART_TX_PIN) ") / RX (GP" BOARD_STR(                 \
		BOARD_UART_RX_PIN) ")"
#ifdef BOARD_HAS_RTC
#define BOARD_RTC_PINS_TEXT                                                                        \
	"I2C" BOARD_STR(BOARD_RTC_I2C) ": GP" BOARD_STR(BOARD_RTC_SDA_PIN) " SDA, GP" BOARD_STR(       \
		BOARD_RTC_SCL_PIN) " SCL"
#endif
// RTS/CTS (the Flow control setting) needs both pins.
#if defined(BOARD_UART_CTS_PIN) && defined(BOARD_UART_RTS_PIN)
#define BOARD_HAS_RTSCTS 1
#define BOARD_UART_FLOW_TEXT                                                                       \
	", RTS (GP" BOARD_STR(BOARD_UART_RTS_PIN) ") / CTS (GP" BOARD_STR(BOARD_UART_CTS_PIN) ")"
#else
#define BOARD_HAS_RTSCTS 0
#endif

#endif
