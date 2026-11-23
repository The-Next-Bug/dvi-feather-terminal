# WiFi modem

A board wired for one (the [Frankless](../start/boards.md#frankless-frankless))
can have an ESP8266 module running
[Zimodem](https://github.com/bozimmerman/Zimodem) on its serial port and
switch the port between a plain serial line and that modem (we build
[our fork](https://github.com/arlaneenalra/Zimodem/tree/frankless-build) of it, with hardware flow control for this wiring). In WiFi modem
mode the terminal resets the module at every start, finds it, sets it up
and gives it the WiFi network from its own settings, so the module never
needs setting up by hand, and a reflashed or replaced one joins by itself.

Calls go out from the modem: [Telnet](telnet.md)'s F11 in terminal mode,
the Z80's [printer](../cpm/printing.md), or a CP/M program talking Hayes
commands on the second serial port (TTY2) in CP/M mode.

*Sources: `src/modem/modem.c` (the start-up), `src/modem/carrier.c` (DCD
and DTR), `src/ui/modempanel.c` (the Modem panel),
`apps/terminal/modem_glue.c` (their glue, the status and HWINFO),
`src/platform/modem_lines.c` (the pins), `boards/frankless.h` (the
wiring), `lib/zimodem` (the Zimodem build for it).*

## Wiring: an ESP-12F by hand

The Frankless's serial port is on J8 (with RTS/CTS); the modem's four
control lines go to J1. Top view, the USB-A end up and the board edge
(audio jack side) on the right; `[ ]` marks the square pin-1 pad. J1's
columns line up with J8's right-hand two.

```
             USB-A end
  J1      odd   even  |
          [1]    2    |   1 VBUS (5 V)   2 GND
           3     4    |   3 GP29         4 GP28
           5     6    |   5 GP27 DTR     6 GP26 DCD
           7     8    |   7 GP25 BOOT    8 GP24 RST
           9    10    |   9 GND         10 3V3
                      |  board edge
  J8   5     3   [1]  |   1 3V3   3 RTS (GP23, out)   5 RX (GP21, in)
       6     4    2   |   2 GND   4 CTS (GP22, in)    6 TX (GP20, out)
             HDMI end
```

ESP-12F pins, antenna up, shield toward you: left side RST, ADC, EN,
GPIO16, GPIO14, GPIO12, GPIO13, VCC; right side TXD, RXD, GPIO5, GPIO4,
GPIO0, GPIO2, GPIO15, GND (top to bottom).

| From | To | Signal | Notes |
|---|---|---|---|
| J8.1 3V3 | VCC, EN (through its pull-up) | power | |
| J8.2 GND | GND | | |
| J8.3 RTS (GP23 out) | GPIO13 (U0CTS) | flow control | |
| J8.4 CTS (GP22 in) | GPIO15 (U0RTS) | flow control | pull-down to GND (a boot strap) |
| J8.5 RX (GP21 in) | TXD | data | |
| J8.6 TX (GP20 out) | RXD | data | |
| J1.6 GP26 (in) | GPIO14 | DCD | low while the modem has a connection open |
| J1.5 GP27 (out) | GPIO12 | DTR | the terminal drops it to hang up |
| J1.8 GP24 (open drain) | RST | reset | pull-up to 3V3 at the module |
| J1.7 GP25 (open drain) | GPIO0 | boot loader | pull-up to 3V3 at the module |
| J1.9 GND | GND | | a second ground, optional |

Pull-ups: GPIO0, GPIO2, EN and RST up to 3V3, GPIO15 down to GND. 4.7k
or 5.1k on RST and GPIO0, not 10k: until the firmware sets GP24-29 they
keep the RP2350's reset pull-downs (about 50k, and erratum RP2350-E9's
leakage), and against those a 10k pull-up gives only about 2.7 V, too close
to the ESP8266's reset threshold; 4.7k or 5.1k gives about 3.0 V. Breakout
boards often pull up EN and pull down GPIO15 already, but not RST or
GPIO0: check. 5.1k for every resistor works (the author's module); what
matters is the direction. A resistor in series on RXD (1k to 5.1k) lets a
USB-serial adapter drive it for flashing and costs nothing at 921600 baud.

Leave J1.1 (VBUS) unused unless the module has its own regulator, and never
feed J8's 3V3 and a regulator together. J1.3 and J1.4 (GP29, GP28) stay
free.

DCD is on GPIO14, the pin the frankless-wifi carrier board uses
(`reworked/frankless-wifi` in the Frankless's board repository: the module,
its own regulator from J1.1 and these lines on one board that plugs onto J1
and J8). Earlier builds used GPIO4: a module wired that way needs the wire
moved to GPIO14 along with the firmware update (the pin is the firmware's
now, below). The carrier uses 10k pull-ups on RST and GPIO0,
which works because the start-up resets the module once the terminal's
firmware has let go of GP24 and GP25.

The pins are in the board header; the [board's table](../start/boards.md)
lists them, and HWINFO shows them (below).

## The Zimodem build

The module runs our Zimodem build (`lib/zimodem`, its own `BUILDING.md`):
hardware RTS/CTS on GPIO13/15 for J8, DCD on GPIO14 and DTR on GPIO12 for
J1, as wired above. The build fixes those pins: whatever the module saved
before is ignored, and `ATS47`, `ATS49`, `ATS51`, `ATS55` refuse other pins,
so a module flashed with it is wired right without any setting. Every
release ZIP has it in `modem/`: `zimodem.bin`, its `build-info.txt` (the
version `ATI4` reports), its sources and its
[licenses](../hacking/licenses.md#the-wifi-modems-firmware);
`modem/README.txt` tells how to flash it with esptool. Zimodem's other
settings (its rate, echo, the DCD and DTR levels, the network) are the
terminal's to set: do not set those by hand, the next start sets them back.

### The XH-C3F (ESP32-C3)

**Not yet tested on hardware**: the build compiles and is packaged, but no
XH-C3F has run it yet.

The frankless-wifi carrier also takes a Sparkleiot XH-C3F, an ESP32-C3 in
the ESP-12F's footprint, and the release ZIP has a Zimodem build for it in
`modem/xhc3f/` (`zimodem-xhc3f.bin`, its `build-info.txt`, sources and
licenses). Its pins are on the same pads under the C3's names: CTS IO5,
RTS IO10, DCD IO3, DTR IO4, boot loader IO9 (IO8 must be pulled up), RST
on EN; fixed by the build in the same way. The terminal cannot write a
C3's firmware yet: flash `zimodem-xhc3f.bin` at address 0 with esptool,
through the C3's own USB (pads 19/20) or a USB-serial adapter on RXD0/TXD0
(`modem/README.txt`). Its boot loader talks at 115200, so the start-up's
first step (the ESP8266's boot text at 74880) reads nothing useful from
it; the rest of the start-up is the same.

## Updating the module's firmware

The terminal writes the module's firmware itself, through the ESP8266's
own boot loader over the serial port, so a module wired as above never
needs a USB-serial adapter (a fresh one, still on Espressif's AT firmware,
included). Copy the release ZIP's `modem/ZIMODEM.ZMI` to the root of the
SD card or a USB stick, open the [Modem panel](setup.md#the-modem-panel)
(Return on SET-UP's Port row) and press **Return** on **Update firmware**:

1. The terminal looks for `ZIMODEM.ZMI` on the card, then the stick, and
   checks all of it before anything is erased: the
   [container](../reference/file-formats.md#zimodemzmi) and its CRC, the
   ESP image in it, and that it is for this chip (ESP8266), this module
   (the board header's `BOARD_MODEM_NAME`, `esp12f`) and fits its flash
   (`BOARD_MODEM_FLASH_KB`). It shows the file's Zimodem version (the
   module's own is on the Firmware row) or why the file cannot be used.
2. **Return** again writes it: BOOT (GPIO0) held low across a reset starts
   the boot loader, which must answer at 115200 baud and say it is an
   ESP8266; then the image's sectors are erased and it is written 1 KB at
   a time, each block checked by the loader. The panel and the status line
   show the progress; about a minute for Zimodem's 400 KB. Keys do nothing
   meanwhile: do not switch off.
3. The module is reset into the new firmware and the usual
   [start-up](#the-start-up) runs: writing it erased Zimodem's saved
   settings, and the start-up gives them back (the rate, the profile, the
   network from SET-UP). Then the panel compares the version the module
   reports with the file's: `Updated: Zimodem ...`, or what it reports
   instead.

The ESP8266's boot loader cannot read the flash back, so the check of the
write is the loader's per-block checksums and the version the new firmware
reports. If anything goes wrong (`Not written: ...` and the reason), the
module is reset and runs whatever its flash holds; a write cut short
leaves no working firmware, and the start-up says `no answer`, but the
boot loader is in the chip's ROM: Update firmware again always works.

## The Port setting

SET-UP's **Port** row (boards wired for a modem only): `Serial` or `WiFi
modem`. Saving a new Port restarts the board. With the WiFi modem the
line is fixed at 921600 baud, 8N1, RTS/CTS: Baud rate, Data/parity/stop
and Flow control show that, marked `(WiFi modem)`, and are skipped, and
CP/M programs cannot change the rate. Return on the Port row opens the
[Modem panel](setup.md#the-modem-panel): the network, the password,
Reset modem, Defaults and what the start-up found.

The network and password are kept in the terminal's flash, the password in
plain text, like the module's own copy.

## The start-up

At every start in WiFi modem mode (power-on, a reboot, a crash), and on
the panel's Reset modem, the terminal:

1. holds the module in reset 20 ms and reads its boot text at 74880 baud
   (the ESP8266's ROM: the reset cause, shown on the panel's Firmware row),
2. waits for an answer to `AT` at 921600. Zimodem joins its saved network
   before it says anything, for up to 30 s, so silence is waited out (up to
   35 s); bytes that are not text mean another rate,
3. otherwise tries the rates in turn (115200 first, 74880, then 460800
   down to 1200) and moves the module to 921600,
4. sets what it needs (`ATI5`'s profile compared, only differences sent):
   echo (off in terminal mode, on for CP/M programs), result codes, DCD and
   DTR on their pins, DTR hanging up a call,
5. gives it the network (`ATW`) when it is not on it, and saves the
   settings in the module (`AT&W`) only after a change,
6. reads its version and address.

Meanwhile the status line starts with `WiFi modem: ` and the step
(`resetting`, `starting`, `finding its rate`, `joining the network`,
`setting it up`), and the serial port is the modem's: the terminal sends
nothing and shows nothing from it, and CP/M's TTY2 and the printer wait.
If no rate answers, it resets the module once more before giving up.

## On the status line

Once the start-up is over the status line shows the modem where the baud
rate and framing were (in CP/M mode before the flow control):

```
 VT100 80x24 | v0.15.0 | wifi RTS/CTS | telnet | kbd | USB                    14:05
```

| Field | Meaning |
|---|---|
| `wifi` | the modem is on a network; lit (reverse video) while it has a connection open (its DCD) |
| `no wifi` | the modem answers, but has no network: set one on the Modem panel |
| `modem?` | the modem did not answer at any rate |

The network state is what the start-up found: the module is not asked
again, so a network lost later still shows `wifi` until the next start-up.

Calls: while DCD is active (any connection the modem has open) local echo
is off and `wifi` is lit; DCD going inactive ends a telnet call. Hanging up
from the Telnet panel drops DTR, which makes Zimodem drop the call at once
(and falls back to `+++` and `ATH` if it does not).

## HWINFO

[HWINFO](../cpm/tools/hwinfo.md) under CP/M lists the modem as item 36,
`WiFi modem`: the module, Zimodem's version, the network and address (and
`connected` during a call), or `no network`, or `missing` when it did not
answer; then the serial port, its rate and the four control lines' pins.
With Port on the serial line it shows `no`, and the pins.

## Troubleshooting

`modem?` or `no answer` on the panel:

- Check the module's power and the four data wires (TXD to J8.5, RXD to
  J8.6, and the two flow control lines, which are crossed: our RTS to its
  GPIO13, our CTS to its GPIO15).
- The module may be stuck in its boot loader: GPIO0 low at reset (check
  its pull-up).
  The Firmware row shows the ROM's last reset cause and boot mode; boot
  mode `(1,x)` is the boot loader, `(3,x)` the flash.
- Reset modem on the panel tries again; Defaults restores the module's
  factory settings first, then the terminal's (it takes a while).
- A module with no working firmware (a write cut short, the wrong image
  flashed by hand) still has its boot loader:
  [Update firmware](#updating-the-modules-firmware) writes ours.

`no wifi` with Problem `it refused the settings`: the module is not
running our Zimodem build (a fresh ESP-12F has Espressif's AT firmware,
which answers `AT` but not Zimodem's commands):
[Update firmware](#updating-the-modules-firmware) writes it.

Update firmware's `Not written: no answer from the loader`: check the
BOOT (J1.7 to GPIO0) and RST (J1.8) wires and their pull-ups; the loader
also needs GPIO15 low and GPIO2 high at reset, as for a normal start.

`no wifi` otherwise: the network's name or password is wrong, or the network is out
of range. The panel's Problem row says what went wrong last. Zimodem
cannot take a comma or a quote in the name, or a quote in the password.

A long start at every boot (`starting` for up to 35 s): Zimodem is trying
a network it cannot join. Set the right one on the Modem panel.
