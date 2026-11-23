# Idea: modem carrier detect and hang-up, and an ESP-12F carrier board

Status: Option A built (bd xir.15, xir.17.5): DCD/DTR/RST/BOOT on J1, by
hand and on the frankless-wifi carrier board (2026-11-22, below). Written
from a read of lib/zimodem (frankless-build d0f904a), the Frankless KiCad
files (~/fz80/pi-pico-boards reworked/frankless, commit 3292573) and its
experimental case (exp-case/frankless.scad, frankless_board.stl). DCD moved
from GPIO4 to GPIO14 with the carrier (bd xir.17.12): the sections below
written before it say GPIO4.

## The problem

The firmware cannot see a call end or end one itself without in-band
guesswork. telnet_glue.c hangs up with 1.5 s quiet, `+++`, 1.5 s, `ATH`
(about 3 s, keys dropped), and nothing tells it the far end closed: Zimodem
prints `NO CARRIER` when it drops back to command mode, but a remote host
can send that text too (the user chose not to watch for it, bd xir.10).
printjob relies on Zimodem's S61 idle timeout and its `OK`.

## Option A: DCD and DTR wires (no Zimodem changes)

Zimodem already drives and reads modem lines on the ESP8266:

| Signal | Direction | Default GPIO | Registers | Behavior |
|---|---|---|---|---|
| DCD | modem -> us | GPIO2 | S47 pin, S46 level | active (low) while any WiFi connection is open (checkOpenConnections); drops before NO CARRIER |
| DTR | us -> modem | GPIO12 | S55 pin, S54 level | with `ATS63=2`, going inactive (high) in stream mode hangs up at once (ZStream::doHangupChecks) |
| RI | modem -> us | GPIO14 | S53, S52 | incoming connections only |
| DSR | modem -> us | 11 (off) on the Frankless | S57, S56 | moved off GPIO13 for hardware CTS; GPIO4/5 are free |

All active low by default. Use GPIO4 for DCD (`ATS47=4`, `AT&W`) rather
than GPIO2, a boot strap pin that must be high at reset and drives the
module's blue LED. Set `ATS63=2` only once our side drives DTR: a floating
or high DTR means "hang up", so with S63=2 and no wire calls drop by
themselves (BUILDING.md says leave S63=0 for that reason).

Frankless pins: J8's six pins are all used (3V3, GND, RTS, CTS, RX, TX), so
the extra lines go to J1. Checked in the PCB: each of GP24-29 has exactly
two pads, the RP2350 pin (U7.36-43) and its J1 pin, nothing else (no pulls,
no I2C or SPI; the RTC's I2C1 is on GP10/11), and boards/frankless.h claims
none of them.

| J1 | Net | Suggested use |
|---|---|---|
| 1 | VBUS (5 V from USB-C) | carrier board's regulator input |
| 2, 9 | GND | |
| 3 / 4 | GP29 / GP28 | keep for a second UART (UART0, bd qtq) |
| 5 | GP27 | DTR out -> ESP GPIO12 |
| 6 | GP26 | DCD in <- ESP GPIO14 (GPIO4 before the carrier) |
| 7 | GP25 | ESP GPIO0 (open drain): boot loader |
| 8 | GP24 | ESP RST (open drain): reset a hung modem |
| 10 | +3V3 | |

Terminal firmware: BOARD_MODEM_DCD_PIN / _DTR_PIN / _RST_PIN / _BOOT_PIN in
boards/frankless.h (board.h's used-once check, HWINFO item); telnet_glue.c
ends the telnet state on carrier loss and hangs up by DTR; status line shows
carrier; printjob can use DCD for a job's end; SIO2's status could report
DCD to CP/M programs. Later: a "reset modem" action, and flashing Zimodem
from the card or a stick through RST/GPIO0 and the UART.

### Variant: everything on J1

GP24-27 are UART1's other pin set (GPIO n is UART ((n + 4) >> 3) & 1, n % 4
= TX, RX, CTS, RTS; board.h accepts them), so the modem can run from J1
alone, J8 unused:

| J1 | GPIO | Function | ESP-12F |
|---|---|---|---|
| 8 | GP24 | UART1 TX | RXD (1k series) |
| 7 | GP25 | UART1 RX | TXD |
| 6 | GP26 | UART1 CTS | GPIO15 (U0RTS), 10k down |
| 5 | GP27 | UART1 RTS | GPIO13 (U0CTS) |
| 4 | GP28 | GPIO | either DCD <- GPIO4, or RST |
| 3 | GP29 | GPIO | either DTR -> GPIO12, or GPIO0 |
| 1, 2/9, 10 | VBUS, GND, 3V3 | | LDO in, ground (3V3 unused) |

Two GPIOs are left after the UART: DCD + DTR (no reset or boot loader),
or RST + GPIO0 with call status and hang-up in-band (Option B2). Zimodem
unchanged either way (FRANKLESS_HW_FLOW). Firmware: a second header
(e.g. boards/frankless-j1.h including frankless.h, the UART pins
overridden, as the Feather variants do), so J8's build stays. Costs:
GP28/29 no longer free for a second UART (that would need J8's GP20-23);
one 2x5 socket is a weak mount, a J8 socket would steady it.

## Hand-wired: UART on J8, control lines on J1

What the user is wiring first (2026-11-18), before any carrier board: the
UART stays on J8 as now, J1 adds DCD, DTR, ESP RST and GPIO0. Firmware
support: bd xir.15.

Header layout, top view, the USB-A end up and the board edge (audio jack
side, x = 32) on the right; [ ] marks the square pin-1 pad. J1's columns
line up with J8's right-hand two.

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

PCB coordinates (KiCad, mm): J1 pin 1 at (217.35, 96.02), even column at
x 219.89, rows 2.54 apart toward +y; J8 pin 1 at (219.91, 111.17), pins 3
and 5 at x 217.37 and 214.83, pins 2/4/6 at y 113.71.

ESP-12F pins, antenna up, shield toward you: left side RST, ADC, EN,
GPIO16, GPIO14, GPIO12, GPIO13, VCC; right side TXD, RXD, GPIO5, GPIO4,
GPIO0, GPIO2, GPIO15, GND (top to bottom).

| From | To | Signal | Notes |
|---|---|---|---|
| J8.1 3V3 | VCC, EN (via its pull-up) | power | as now |
| J8.2 GND | GND | | as now |
| J8.3 RTS (GP23 out) | GPIO13 (U0CTS) | flow | as now |
| J8.4 CTS (GP22 in) | GPIO15 (U0RTS) | flow | as now; 10k to GND (boot strap) |
| J8.5 RX (GP21 in) | TXD | data | as now |
| J8.6 TX (GP20 out) | RXD | data | as now |
| J1.6 GP26 (in) | GPIO14 | DCD | fixed by our Zimodem build (was GPIO4, `ATS47=4`); active low = connected |
| J1.5 GP27 (out) | GPIO12 | DTR | keep `S63=0` until the firmware drives it |
| J1.8 GP24 (open drain) | RST | reset | 4.7k pull-up to 3V3 at the module |
| J1.7 GP25 (open drain) | GPIO0 | boot loader | 4.7k pull-up to 3V3 at the module |
| J1.9 GND | GND | | second ground, optional |

J1.1 VBUS stays unused until there is a regulator on the module side
(never feed J8's 3V3 and a regulator together); J1.3/J1.4 (GP29/GP28)
spare.

Why 4.7k on RST and GPIO0: until the firmware sets GP24-29 they keep the
RP2350's reset pulls (about 50k down, and RP2350-E9's leakage). A 10k
pull-up gives only ~2.7 V, too close to the ESP's reset threshold; with
4.7k it is ~3.0 V, so the modem boots normally under today's firmware.
Breakout boards often already pull up EN and pull down GPIO15 but not RST
or GPIO0: check. DTR and DCD are harmless before then: GP27's pull-down
reads as DTR active, and S63=0 ignores DTR anyway.

The user's dead-bug module uses 5.1k for every resistor (2026-11-18):
fine. RST and GPIO0 against the ~50k pull-downs sit at ~3.0 V, as with
4.7k; the GPIO15 pull-down takes ~0.65 mA from U0RTS when high; 5.1k on
EN or GPIO2 is a firmer pull than the usual 10k. A 5.1k in series on RXD
(instead of 1k) rounds edges by ~25 ns against a 1.1 us bit at 921600.
What matters is the direction: GPIO15 down, GPIO0, GPIO2, EN, RST up.

A newer module (ESP32-C3/S3 with Zimodem's SSH) would wire the same J1
lines to its own boot pins: docs/ideas/esp-at-modem.md, "Newer modules".

## Option B: in-band, no new wires

1. Zimodem's packet mode, already there: `ATC"host:port"` stays in command
   mode; data arrives as `[ id len crc8 ]` CR LF + bytes (S40, default 127),
   we send with `ATTn` + n bytes; `NO CARRIER id host:port` cannot be faked
   inside framed data; `ATH0` hangs up at once; `ATC0` lists connections.
   Costs: a framed protocol both ways in telnet_glue.c (and printjob), ~11%
   overhead on incoming data, every key burst through Zimodem's command
   parser. Its one advantage: several connections at once (print during a
   call).
2. An escaped stream, a small Frankless extension to our fork (behind a new
   S register, stock behavior the default): one reserved byte, e.g. DLE,
   doubled in data like telnet's IAC; modem -> us DLE `C` = closed (before
   the drop to command mode), more codes for WiFi lost etc.; us -> modem DLE
   `H` = hang up now. Near-zero overhead, the stream unchanged. Zimodem:
   ZStream::loop (escaping, the close event) and the serial input next to
   checkPlusPlusPlusEscape; us: a few states before telnet_glue_filter and
   DLE doubling in telnet_glue_send. The better in-band choice.
3. GSM 07.10 / 27.010 CMUX: the standard for virtual modem lines over one
   UART, too heavy here.

## A carrier board for the ESP-12F

### As built: frankless-wifi (2026-11-22)

The user's board, pi-pico-boards `reworked/frankless-wifi` (main d098ea0,
PR #3; production Frankless_Frank_Wifi_1.00.zip), for an ESP-12F or a
Sparkleiot XH-C3F (ESP32-C3 in the same 22-pad footprint; bd xir.17.12).
Module pads (ESP-12F / XH-C3F name):

| Pad | Module | Carrier |
|---|---|---|
| 1 | RST / EN | J1.8 GP24 through R5 100R; C2 1 uF to GND, R1 10k up |
| 3 | EN / IO2 | R2 10k up |
| 5 | GPIO14 / IO3 | J1.6 GP26: DCD |
| 6 | GPIO12 / IO4 | J1.5 GP27: DTR |
| 7 | GPIO13 / IO5 | J8.3 RTS (GP23 out): the module's CTS |
| 8 | VCC | 3V3 from U3 (AMS1117-3.3 on J1.1 VBUS); 22 uF x3 + 100 nF |
| 15 | GND | GND |
| 16 | GPIO15 / IO10 | J8.4 CTS (GP22 in): the module's RTS; R4 10k down |
| 17 | GPIO2 / IO8 | R6 10k up (the C3's download mode needs IO8 high) |
| 18 | GPIO0 / IO9 | J1.7 GP25: BOOT; R3 10k up |
| 21 / 22 | RXD / TXD | J8.6 TX / J8.5 RX |
| 2, 4, 9-14, 19, 20 | | unconnected (19/20: the C3's USB, unused) |

J8.1 and J1.10 (the Frankless's 3V3) are unconnected. The RC on pad 1
rises past the ESP's threshold in ~14 ms, inside ESPFLASH_BOOT_MS (100 ms)
and MODEM_ROM_MS (300 ms). 10k on RST and BOOT works because the start-up
resets the module after the firmware releases GP24/25.

Fit in exp-case (by J1 pin 1; case coordinates as below): board x
-0.1..31.9, y 16.6..43.4, notch for the button-plunger guide x 19.1..25.7
(KiCad 224.36..230.95, 0.5 mm radius at its mouth, so module pad 22 keeps
0.5 mm to the edge), y >= 35.4; module body x 1.8..17.8, y 26.5..50.5, the
antenna 7.1 mm past the board edge. Clearances: guide 0.32 / 0.44 mm, LD2's
light-pipe tube 0.17 mm, walls 0.20 / 0.37 mm, posts 9.8 mm; only the
header pins (z 8.54) under the board. All copper >= 0.5 mm from the edge;
DRC passes (user).

The plan it was drawn from:

Plugs onto J1 and J8, carrying the module, its own regulator and Option A's
lines.

### Fit on the board

From frankless.kicad_pcb: J1 (2x5) and J8 (2x3) are on one 0.1" grid
within about 0.1 mm (J1's columns line up with J8's two right-hand ones;
J1's bottom row to J8's top row 4.99 mm, 2 pitches 5.08): check with
calipers first. Both are about 2.4 mm from the board edge (x 222.3). J2, a
3-pin JST SH (debug), is just past J8 at about (218.3, 118.1): keep clear.

### Electrical

- Power from J1.1 VBUS through its own LDO (AP2112K-3.3 600 mA, or
  LDL1117 / AMS1117 with copper): the Frankless's AMS1117 (U3) would drop
  1.7 V at the ESP8266's 300-400 mA transmit peaks on top of the board's
  load, about 1 W in SOT-223. Leave J8.1 (3V3) unconnected. 10 uF in,
  22 uF out, 100 uF low-profile (1210 ceramic or polymer) + 100 nF at the
  module.
- ESP-12F: EN 10k up; RST 10k up (+100 nF optional); GPIO0 10k up; GPIO2
  10k up; GPIO15 10k down (boot strap, also U0RTS).
- J8.3 RTS (GP23) -> GPIO13 (U0CTS); J8.4 CTS (GP22) <- GPIO15 (U0RTS);
  J8.5 RX (GP21) <- TXD; J8.6 TX (GP20) -> RXD through 1k, so a USB-serial
  adapter can drive RXD.
- 6-pin FTDI-style programming header as the fallback; RST/BOOT buttons for
  the bench only (not reachable in the case).
- 2-layer, about 30 x 26 mm, the module, an LDO, a dozen 0603 parts, female
  headers. A new project beside reworked/frankless in pi-pico-boards.

### Space in the case

exp-case coordinates: origin at the board corner at the HDMI end, USB-C
side; x across (0-32), y along (0-74, toward USB-A), z = 0 the board's top
face. Heights from frankless_board.stl, each triangle's bounding box taken
as occupied (conservative).

- Lid underside at z 16.65 (USB-A 16.25 + 0.4); outside 18.65. Inner walls
  0.3 mm from the board edge (x -0.3..32.3), corner radius 3.3: no overhang
  past the board edge inside the case.
- Away from the USB-A stack and the header pin models nothing exceeds
  ~6 mm (HDMI and audio shells ~6, slide switch S2 ~5, most parts <= 3).
- Above z ~8 only these block: lid posts (3, 71), (29, 71), (29, 3),
  (3, 2.9), 7.6 mm across; LED pipe tubes (3.1, 61.2), (5.2, 14.7), 3.6 mm
  across from z 2.1; the button-plunger guide x 19.5-25.3, y 36.6-46.8 from
  z 3.8; USB-A at y >= 59, x 9-24; J10's pins at (26.75..29.29, 8.6) if
  fitted (trim them).
- Pin fields: J1 x 25.8-30.9, y 25.2-37.9; J8 x 23.3-30.9, y 17.6-22.7
  (with Dupont outlines).

Stack: female sockets (8.5 mm) on the Frankless, male pins down from the
carrier: carrier bottom 8.5, top 10.1 (1.6 mm PCB), ESP-12F top 13.3,
3.3 mm to the lid; ~3 mm under the carrier for small parts (not over the
5-6 mm HDMI / audio / S2 spots). Male pins on the Frankless with sockets on
the carrier puts the module top at 15.8 (0.85 mm margin): too tight. The
case's `ff_over` raises the lid if more is needed.

Layout that fits: carrier x 2-32, y 15-41 with a notch at x 19.2-25.6,
y >= 36.3 for the button guide; J1/J8 sockets at x 23-31, y 17-38; module
long axis along y at x 3-19, body y 24-41, antenna overhanging the carrier
edge to y 48 (no copper under it), as high as it goes (z ~10-13), over the
board's low area (x 3-19, y 38-48), away from the USB-A and audio shells.
The ground plane stays 10-13 mm below: expect less range than the bare
module. Getting the antenna outside needs a lid variant (a bump or slot on
the x = 32 side).

Before drawing it: a frankless_carrier() keepout/placeholder in
frankless.scad and the README's point-sampling interference check against
frankless_board.stl (deferred, user 2026-11-18).
