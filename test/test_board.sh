#!/usr/bin/env bash
# src/board/board.h's compile-time checks: every boards/*.h passes them,
# and pin mistakes stop the build with the right message. board.h only
# needs pico.h for the board header, so a stub stands in for the SDK.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
failures=0

# check NAME EXPECT [LINE...]: board.h with the stub pico.h, whose last
# lines are LINE...; EXPECT is "" to pass, else part of the #error text.
check() {
  local name=$1 expect=$2 out
  shift 2
  {
    echo '#define pico_board_cmake_set(var, value)'
    echo '#define pico_board_cmake_set_default(var, value)'
    printf '%s\n' "$@"
    echo '#if PICO_RP2350A'
    echo '#define NUM_BANK0_GPIOS 30u'
    echo '#else'
    echo '#define NUM_BANK0_GPIOS 48u'
    echo '#endif'
  } > "$work/pico.h"
  if out="$("$CC" -fsyntax-only -I"$work" -I"$ROOT/lib/pico-sdk/src/boards/include" \
      -x c "$ROOT/src/board/board.h" 2>&1)"; then
    [[ -z "$expect" ]] && return
    echo "FAIL $name: built, expected: $expect"
  else
    [[ -n "$expect" && "$out" == *"$expect"* ]] && return
    echo "FAIL $name: expected ${expect:-success}, got:"
    echo "$out"
  fi
  failures=$((failures + 1))
}

for board in "$ROOT"/boards/*.h; do
  for flow in 0 1; do
    check "$(basename "$board") flow $flow" "" "#define SERIAL_FLOW_CONTROL $flow" "#include \"$board\""
  done
done

# The DVI Sock's pins, to change one or two at a time.
sock='#include "'"$ROOT"'/boards/pico2-dvi-sock.h"'
set_pin() { echo "#undef $1"; echo "#define $1 $2"; }

check "clock pair swapped" "" "$sock" "$(set_pin BOARD_DVI_CLK_P_PIN 15)" "$(set_pin BOARD_DVI_CLK_N_PIN 14)"
check "lanes reordered" "" "$sock" "$(set_pin BOARD_DVI_D0_P_PIN 18)" "$(set_pin BOARD_DVI_D0_N_PIN 19)" \
  "$(set_pin BOARD_DVI_D1_P_PIN 12)" "$(set_pin BOARD_DVI_D1_N_PIN 13)"
check "DVI pin twice" "each of GP12-19" "$sock" "$(set_pin BOARD_DVI_D1_N_PIN 12)"
check "DVI pin off the HSTX" "each of GP12-19" "$sock" "$(set_pin BOARD_DVI_D1_N_PIN 20)"
check "UART1" "" "$sock" "$(set_pin BOARD_UART 1)" "$(set_pin BOARD_UART_TX_PIN 8)" \
  "$(set_pin BOARD_UART_RX_PIN 9)" "$(set_pin BOARD_UART_CTS_PIN 10)" "$(set_pin BOARD_UART_RTS_PIN 11)"
check "UART aux TX/RX" "" "$sock" "#undef BOARD_UART_CTS_PIN" "#undef BOARD_UART_RTS_PIN" \
  "$(set_pin BOARD_UART_TX_PIN 2)" "$(set_pin BOARD_UART_RX_PIN 3)"
check "TX of the other UART" "not a TX pin" "$sock" "$(set_pin BOARD_UART_TX_PIN 4)"
check "RX on a TX pin" "not an RX pin" "$sock" "$(set_pin BOARD_UART_RX_PIN 28)"
check "RTS on a CTS pin" "not an RTS pin" "$sock" "$(set_pin BOARD_UART_RTS_PIN 2)"
check "UART 2" "must be 0 or 1" "$sock" "$(set_pin BOARD_UART 2)"
check "no UART" "must set BOARD_UART" "$sock" "#undef BOARD_UART"
check "SDK UART differs" "PICO_DEFAULT_UART and its pins differ" "$sock" "$(set_pin PICO_DEFAULT_UART_TX_PIN 16)"
check "flow control, no CTS" "needs BOARD_UART_CTS_PIN" "$sock" "#undef BOARD_UART_CTS_PIN" \
  "#define SERIAL_FLOW_CONTROL 1"
check "no flow control pins" "" "$sock" "#undef BOARD_UART_CTS_PIN" "#undef BOARD_UART_RTS_PIN"
check "SD on SPI1" "" "$sock" "$(set_pin BOARD_SD_SPI 1)" "$(set_pin BOARD_SD_MISO_PIN 8)" \
  "$(set_pin BOARD_SD_CS_PIN 9)" "$(set_pin BOARD_SD_SCK_PIN 10)" "$(set_pin BOARD_SD_MOSI_PIN 11)"
check "SCK of the other SPI" "not an SCK pin" "$sock" "$(set_pin BOARD_SD_SCK_PIN 10)"
check "MOSI on MISO" "not a TX pin" "$sock" "$(set_pin BOARD_SD_MOSI_PIN 4)"
check "CS on UART TX" "uses a pin twice" "$sock" "$(set_pin BOARD_SD_CS_PIN 0)"
check "CS on a DVI pin" "DVI pin (GP12-19) another job" "$sock" "$(set_pin BOARD_SD_CS_PIN 13)"
check "VBUS enable" "" "$sock" "#define BOARD_USB_VBUS_EN_PIN 22"
check "VBUS enable on the LED" "uses a pin twice" "$sock" "#define BOARD_USB_VBUS_EN_PIN 25"
check "GPIO 33 on an RP2350A" "does not have" "$sock" "#define BOARD_USB_VBUS_EN_PIN 33"
check "PSRAM CS1 on GP8" "" "$sock" "#define BOARD_PSRAM_CS_PIN 8"
check "PSRAM CS1 not an XIP_CS1 pin" "not an XIP_CS1 pin" "$sock" "#define BOARD_PSRAM_CS_PIN 9"
check "PSRAM CS1 on the UART" "uses a pin twice" "$sock" "#define BOARD_PSRAM_CS_PIN 0"
check "PSRAM CS1 on a DVI pin" "DVI pin" "$sock" "#define BOARD_PSRAM_CS_PIN 19"
rtc() { echo "#define BOARD_RTC_DS3231 1"; echo "#define BOARD_RTC_I2C $1"; echo "#define BOARD_RTC_SDA_PIN $2"
  echo "#define BOARD_RTC_SCL_PIN $3"; }
check "RTC on I2C1" "" "$sock" "$(rtc 1 10 11)" "#define BOARD_RTC_SQW_PIN 8"
check "RTC on I2C0" "" "$sock" "$(rtc 0 8 9)"
check "RTC SDA of the other I2C" "not an SDA pin" "$sock" "$(rtc 1 8 11)"
check "RTC SDA on an SCL pin" "not an SDA pin" "$sock" "$(rtc 1 11 10)"
check "RTC SCL of the other I2C" "not an SCL pin" "$sock" "$(rtc 1 10 9)"
check "RTC I2C 2" "must be 0 or 1" "$sock" "$(rtc 2 10 11)"
check "RTC without its pins" "needs BOARD_RTC_I2C" "$sock" "#define BOARD_RTC_DS3231 1"
check "RTC pins without a chip" "need a chip" "$sock" "#define BOARD_RTC_I2C 1"
check "RTC on the SD card" "uses a pin twice" "$sock" "$(rtc 0 4 5)"
check "RTC SQW on the LED" "uses a pin twice" "$sock" "$(rtc 1 10 11)" "#define BOARD_RTC_SQW_PIN 25"
i2s() { echo "#define BOARD_AUDIO_I2S_BCK_PIN $1"; echo "#define BOARD_AUDIO_I2S_WS_PIN $2"
  echo "#define BOARD_AUDIO_I2S_DATA_PIN $3"; }
check "I2S DAC" "" "$sock" "$(i2s 9 10 11)"
check "I2S WS not after BCK" "must be BOARD_AUDIO_I2S_BCK_PIN + 1" "$sock" "$(i2s 9 11 10)"
check "I2S without DATA" "needs BOARD_AUDIO_I2S_BCK_PIN" "$sock" "#define BOARD_AUDIO_I2S_BCK_PIN 9" \
  "#define BOARD_AUDIO_I2S_WS_PIN 10"
check "I2S on the SD card" "uses a pin twice" "$sock" "$(i2s 4 5 8)"
check "planned hardware" "" "$sock" "#define BOARD_BUZZER_PIN 8" "#define BOARD_NEOPIXEL_PIN 9" \
  "#define BOARD_SD_CD_PIN 10" "#define BOARD_BUTTON_PIN 11" "#define BOARD_BATTERY_ADC_PIN 26" \
  "#define BOARD_CHARGER_STAT_PIN 27"
check "button on the LED" "uses a pin twice" "$sock" "#define BOARD_BUTTON_PIN 25"
check "battery not on an ADC pin" "not an ADC pin" "$sock" "#define BOARD_BATTERY_ADC_PIN 22"
check "battery on GP40 of an RP2350A" "not an ADC pin" "$sock" "#define BOARD_BATTERY_ADC_PIN 40"
check "no LED" "" "$sock" "#undef PICO_DEFAULT_LED_PIN"
check "DVI drive 8 mA" "" "$sock" "#define BOARD_DVI_DRIVE_MA 8"
check "DVI drive 6 mA" "must be 2, 4, 8 or 12" "$sock" "#define BOARD_DVI_DRIVE_MA 6"
check "no SD pins" "must set BOARD_SD_SPI" "$sock" "#undef BOARD_SD_SPI"
check "no DVI pins" "must set every BOARD_DVI" "$sock" "#undef BOARD_DVI_D2_N_PIN"

if ((failures)); then
  echo "$failures board check(s) failed"
  exit 1
fi
echo "board checks passed"
