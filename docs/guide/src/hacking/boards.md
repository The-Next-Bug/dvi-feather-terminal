# Adding a board

Every pin the firmware uses comes from a board header, `boards/<target>.h`,
built with `TARGET=<target> scripts/build.sh`. `boards/example.h` is the
template, with every setting explained; copy it and change the pins.

A board header is a pico-sdk board header (RP2350 variant, flash size,
crystal: include one of the SDK's, as the Pico DVI Sock's includes
`boards/pico2.h`) plus the firmware's settings:

| Setting | Required | Meaning |
|---|---|---|
| `BOARD_DVI_{CLK,D0,D1,D2}_{P,N}_PIN` | yes | the four TMDS pairs: GP12-19 (the HSTX pins), any order, either polarity (+ and - set separately, so a pair routed crossed is fixed by swapping its two numbers) |
| `BOARD_DVI_DRIVE_MA`, `BOARD_DVI_SLEW_FAST` | no | pad drive (2, 4, 8, 12 mA) and slew; default 2 mA, limited |
| `BOARD_UART`, `BOARD_UART_TX_PIN`, `BOARD_UART_RX_PIN` | yes | the terminal's UART (0 or 1) and its pins |
| `BOARD_UART_CTS_PIN`, `BOARD_UART_RTS_PIN` | no | for RTS/CTS flow control; without them SET-UP offers XON/XOFF only |
| `BOARD_SD_SPI`, `BOARD_SD_{MISO,CS,SCK,MOSI}_PIN` | yes | the SD card's SPI and pins (CS any GPIO) |
| `BOARD_USB_VBUS_EN_PIN`, `_LEVEL` | no | a pin that switches on the USB port's 5 V (on first thing at boot) |
| `PICO_DEFAULT_LED_PIN` | no | the status LED |
| `BOARD_PSRAM_CS_PIN` | no | a PSRAM on the QSPI bus's second chip select (GP0, 8, 19 or 47) |
| `BOARD_RTC_DS3231` or `BOARD_RTC_PCF8523`, `BOARD_RTC_I2C`, `BOARD_RTC_SDA_PIN`, `BOARD_RTC_SCL_PIN` | no | a real-time clock chip on I2C |
| `BOARD_RTC_SQW_PIN` | no | the DS3231's 1 Hz output |
| `BOARD_AUDIO_I2S_{BCK,WS,DATA}_PIN`, `BOARD_AUDIO_I2S_NAME` | no | an I2S DAC for sound (WS = BCK + 1) |
| `BOARD_SYS_KHZ` | no | the CPU clock, a multiple of 48 MHz (default 384000) |

And as CMake settings, in the header as `pico_board_cmake_set(...)`:

| Setting | Meaning |
|---|---|
| `PSRAM_BANKS` | 1: the Z80's banks in the PSRAM |
| `BOARD_DVI_MODE` | the default display mode, `1024x600` or `800x600` |

`src/board/board.h` checks the header when it compiles: a missing setting,
a pin that cannot carry its signal, or a pin used twice stops the build
with an error naming the setting (`test/test_board.sh` covers the checks).
Pin values must be plain numbers: they are shown in on-screen messages.

The pins of the boards so far are in [Boards](../start/boards.md).
Headers for boards that have never run the firmware go in
`boards/experimental/`, which CI and releases leave out.
