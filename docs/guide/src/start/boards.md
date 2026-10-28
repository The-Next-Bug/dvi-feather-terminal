# Boards

The firmware runs on RP2350 boards with the DVI signals on GP12-GP19 (the
RP2350's HSTX pins). Each supported board has a header in `boards/` and its
own firmware file in the release ZIP (`dvi_terminal-<target>.uf2`). The
pins below are read from those headers when this guide is built.

Everything but the DVI connector is optional: the serial port for the
terminal, the SD card for the CP/M machine, and the USB port for a keyboard.

## Pico 2 with the Pico DVI Sock (`pico2-dvi-sock`)

A Raspberry Pi Pico 2 with the Pico DVI Sock for the DVI/HDMI connector:
the default target.

{{#include ../generated/boards/pico2-dvi-sock.md}}

| Pico 2 pin | Signal | Connects to | Needed for |
|---|---|---|---|
| GP0 (pin 1) | UART0 TX, data out | serial adapter RX | the terminal |
| GP1 (pin 2) | UART0 RX, data in | serial adapter TX | the terminal |
| GND | ground | serial adapter GND | the terminal |
| GP3 (pin 5) | RTS out: the terminal is ready to receive | serial adapter CTS | [RTS/CTS flow control](host.md#flow-control) only |
| GP2 (pin 4) | CTS in: the host is ready to receive | serial adapter RTS | RTS/CTS flow control only |
| GP4 (pin 6) | SPI0 MISO | SD card DO | the [CP/M machine](../cpm/overview.md) |
| GP5 (pin 7) | SD card CS | SD card CS | the CP/M machine |
| GP6 (pin 9) | SPI0 SCK | SD card CLK | the CP/M machine |
| GP7 (pin 10) | SPI0 MOSI | SD card DI | the CP/M machine |
| 3V3 (pin 36), GND | power | SD card VCC, GND | the CP/M machine |
| VBUS (pin 40) | 5 V in | a 5 V supply | a [USB keyboard](../terminal/keyboard.md), unless a powered USB OTG adapter powers it |

The SD card adapter must be an SPI one for 3.3 V.

## Frankless (`frankless`)

A reworked MicroFRANK, a board of the [FRANK platform](https://frank.rh1.tech/);
its design files are in
[pi-pico-boards/reworked/frankless](https://github.com/arlaneenalra/pi-pico-boards/tree/main/reworked/frankless).
UART1 on header J8 with CTS/RTS, 8 MB of PSRAM
(the Z80's memory banks and a RAM disk), a DS3231M real-time clock with
its 1 Hz output, a TDA1387 DAC for sound, and a USB hub; a slide switch
puts the USB port on the hub (host) or on the USB-C socket (for flashing).

{{#include ../generated/boards/frankless.md}}

## Adafruit Feather RP2350 with HSTX Port

The Feather with Adafruit's DVI breakout on its 22-pin HSTX connector, and
an Adalogger FeatherWing for the SD card. Four targets:

- `feather-rp2350-hstx`: the plain Feather.
- `feather-rp2350-hstx-psram`: with its 8 MB PSRAM fitted: the Z80's banks
  in the PSRAM (15 of them) and a RAM disk.
- `feather-rp2350-hstx-adalogger`, `feather-rp2350-hstx-psram-adalogger`:
  the same, using the Adalogger FeatherWing's PCF8523 real-time clock on
  the Feather's SDA/SCL (GP2/3), which takes the pins RTS/CTS would use.

A USB keyboard plugs into the Feather's USB-C port through an OTG adapter,
powered (an adapter with its own supply) or with 5 V fed into the
Feather's USB pin; see [Keyboard](../terminal/keyboard.md).

The Feathers run the CPU at 336 MHz instead of 384: two of them crashed at
random at 400 MHz. The display is the same; the Z80 runs about 12% slower.

### feather-rp2350-hstx

{{#include ../generated/boards/feather-rp2350-hstx.md}}

### feather-rp2350-hstx-psram

{{#include ../generated/boards/feather-rp2350-hstx-psram.md}}

### feather-rp2350-hstx-adalogger

{{#include ../generated/boards/feather-rp2350-hstx-adalogger.md}}

### feather-rp2350-hstx-psram-adalogger

{{#include ../generated/boards/feather-rp2350-hstx-psram-adalogger.md}}

## Other boards

Headers for boards that have never run the firmware are in
`boards/experimental/` (the stock MicroFRANK and the PicoTop Rev2); the
release leaves them out. To run on a board of your own, see
[Adding a board](../hacking/boards.md).

The project's name comes from where it started: the Adafruit Feather
RP2040 DVI, supported up to v0.4.0. The firmware outgrew the RP2040, so
RP2040 builds were dropped; the name stayed.
