# Plan: WiFi modem mode (the ESP-12F on J8 and J1)

Status: planned with the user, 2026-11-19; sections 1-6 built on branch
wifi-modem (bd xir.15, xir.17.1-17.6), see "Progress" at the end. Beads: the
"WiFi modem mode" epic under xir, plus xir.15 (pins and start-up state).
The hardware is the user's hand-wired ESP-12F running our Zimodem build
(lib/zimodem): UART with RTS/CTS on J8, DCD, DTR, RST and GPIO0 on J1.
Wiring: modem-carrier.md, "Hand-wired: UART on J8, control lines on J1".
Other modules: esp-at-modem.md ("Newer modules").

Goal: switch the terminal UART between a plain serial port and a WiFi
modem the firmware sets up by itself, so the user never configures the
module by hand, in terminal or CP/M mode.

Decisions (user, 2026-11-19):

- WiFi modem is a choice (Port setting), not a fixed property of the board.
- The link runs at a fixed 921600 with RTS/CTS (xir.12 ran it with RTS
  never lit). A module found at another rate is moved to it.
- The rate scan includes 115200 (the Frankless build's default) and 74880
  (the ESP8266 ROM's rate).
- WiFi credentials are kept in our settings (and pushed to the module), so
  a reflashed or swapped module joins by itself. A scan list in the panel
  is a nice-to-have.
- DHCP to start; DHCP/static as a setting later.
- The settings record grows to 512 bytes if that is safe, otherwise a
  separate record (below: it is safe).
- The Modem panel opens from the Port row in SET-UP (Return: edit) when
  WiFi modem is selected.
- CP/M mode: programs talk Hayes to Zimodem directly on SIO2, as now; our
  profile is applied at start-up only.
- Flashing the module checks that the image matches the board; ESP32
  modules may replace the ESP-12F later.
- A build option (user, 2026-11-19): CMake WIFI_MODEM, off unless the
  board header turns it on (pico_board_cmake_set(WIFI_MODEM, 1): the
  Frankless), and refused without PSRAM_BANKS for now, as the SRAM-only
  boards have too little SRAM left. Without it the settings record stays
  one page and the modem lines stay as at reset.

## 1. The Port setting

Field 27, Port: Serial / WiFi modem (append only). Offered only on boards
whose header names a modem (section 2). A change saves and reboots, as
Mode does. In WiFi modem mode the main SET-UP rows read:

```
Port            WiFi modem      [Return: edit]
Baud rate       921600 (WiFi modem)
Flow control    RTS/CTS (WiFi modem)
```

Baud and Flow are fixed (settings_field_fixed, as Columns under the
ADM-3A); apply_line uses BOARD_MODEM_BAUD and RTS/CTS. In Serial mode Port
has no edit and Baud/Flow are as today.

## 2. Board header

boards/frankless.h:

- BOARD_MODEM_DCD_PIN 26, _DTR_PIN 27, _RST_PIN 24, _BOOT_PIN 25. Roles
  are generic: RST goes to the chip's reset/enable (ESP8266 RST, ESP32
  EN), BOOT to its boot-loader strap (ESP8266 GPIO0, ESP32-C3 GPIO9).
- BOARD_MODEM_CHIP (ESP8266), BOARD_MODEM_NAME ("esp12f": what a module
  image must name), BOARD_MODEM_BAUD 921600.
- The modem is on the terminal UART (BOARD_UART); CTS/RTS required.

board.h: used-once and range checks; #error for modem pins without
CTS/RTS, for some of the four lines but not all, or without chip/name/
baud. example.h documents them, test_board.sh's broken variants,
gen_tables.py's board pin tables. Early in main(): DTR driven active
(low), RST and BOOT released (input, pulls off: RP2350-E9), DCD input with
pulls off; in Serial mode they stay so.

## 3. Modem start-up (src/modem/, portable, host-tested)

Built (bd xir.17.2): src/modem/modem.[ch] (test_modem against a fake
Zimodem), apps/terminal/modem_glue.c. From power-on in WiFi modem mode
(started before the logo; the logo's wait, the main loop and the CPU's
waits poll it), and while it runs the line is its own: the terminal reads
nothing, CP/M's SIO2 gets nothing and its output is dropped, keys and
telnet dials are dropped, print jobs wait.

1. Reset: RST low 20 ms, released.
2. ROM: 300 ms at 74880; the line with "rst cause" kept (the panel will
   show it; boot mode (1,x) = GPIO0 low).
3. At 921600, AT probes (bare CR, AT, CR) every 500 ms from 1 s, every 2 s
   after 2 s. Zimodem in the Frankless build joins its saved network before
   it says anything, for up to 30 s (WIFI_CONNECT_TRIES 60 x 500 ms), so
   silence is not a wrong rate: an OK (or READY.) means found; noise (4
   bytes that are not text: the module talking at another rate) or 35 s
   of silence starts the scan.
4. Scan 115200, 74880, 460800, 230400, 57600, 38400, 19200, 9600, 2400,
   1200, 300 ms each. Found: ATB921600 (Zimodem answers at the new rate),
   our UART switched, an OK, AT&W at the end. Nothing: a second reset, then
   "no answer at any rate".
5. Profile: ATI5 (one line: ATB..E1Q0V1X1F0R1...S46=..S47=..S54=..S55=..
   S63=..) parsed, and one command for what differs: E (0 terminal mode, 1
   CP/M), Q0, V1, F0, R1, S46=0 (DCD active low, Zimodem's default despite
   its README), S47=4, S54=0 (DTR active low), S55=12, S63=2 (DTR hang-up).
6. Network: ATI3 (the SSID it is set to), ATI2 (its address, 0.0.0.0 when
   not joined). Ours set and the module not on it (or not joined):
   ATW"ssid,password" (up to 40 s). Zimodem splits at the first comma and a
   quote ends the text: an SSID with a comma or quote, or a password with a
   quote, is not sent (said so). Ours empty: the module's own kept.
7. AT&W only if something changed (no flash wear on a normal boot), ATI4
   (the version), ready.

Until the Modem panel (xir.17.4), Return on SET-UP's Port row shows a line
from modem_glue_describe: the step, or the version, SSID and address, and
the last problem.

## 4. Calls

telnet_glue: hang-up = DTR inactive >= 100 ms, wait for DCD inactive, DTR
active again (+++/ATH as the fallback when DCD does not drop); DCD
inactive = the far end went (telnet state forgotten, status line).
printjob: DCD ends a job's connection. CP/M: Hayes on SIO2 as now; DCD/DTR
in SIO2's status, optional later.

## 5. Network settings in our flash

SSID (32), password (63), address mode (DHCP; static later: IP, mask,
gateway, DNS; Zimodem may need a Frankless command for static).

Room: the 256-byte record is full (header and 27 fields to ~byte 40, the
text blocks from byte 77). Growing it is safe without a version change:
each settings sector holds one record at its start, so the record becomes
two flash pages and page 0 keeps today's layout exactly; the network block
('N', length, CRC-32, the texts) lives in page 1. Older firmware reads
page 0 and decodes it as before (a downgrade keeps the settings); a save
by older firmware erases the sector and writes page 0 only, so page 1 is
erased: no block, the defaults (network forgotten, nothing worse).
nvflash writes 512 bytes (main.c's static_assert on the page size
changes); interrupts stay off a page longer.

The password is plain text in our flash: the guide says so.

## 6. The Modem panel

Return on the Port row (WiFi modem). Portable (test_setup), cold, slow
work from the main loop (as drives_work, clock_service).

| Row | |
|---|---|
| Network | SSID typed (Return: scan list, later) |
| Password | typed, masked |
| Address | DHCP (Static later, with its fields) |
| Status | SSID, IP, RSSI / not joined / modem? (read-only) |
| Firmware | version, ROM reset cause, ATI12 (read-only) |
| Reset modem | Return: section 3 again |
| Defaults | Return: Zimodem's factory reset, then profile and network |
| Update | Return: section 8; or Zimodem's AT&U |

Return saves and pushes the network to the module; Esc back to SET-UP.

## 7. Status line, HWINFO, guide

Status line: wifi / no wifi / modem?, and the call from DCD. HWINFO: a
modem record (chip, version, IP, lines wired). Guide: a "WiFi modem" page
with the wiring (modem-carrier.md's tables, the 5.1k notes), the Port
setting, the panel, start-up and what "modem?" means.

## 8. Flashing the module (later)

Container (written by lib/zimodem's build script next to the .bin, e.g.
ZIMODEM.ZMI, shipped in the release ZIP): magic, chip family, module
name, flash size, Zimodem version, (address, data) parts, CRC-32.

Checks before erasing: the container's CRC; the ESP image header (0xE9,
its flash size); chip and module name against BOARD_MODEM_CHIP/_NAME; the
chip that answers in the loader (its identity registers, as esptool
detects the chip); the image fits the module's flash. A mismatch refuses,
with the reason, nothing written.

Writing: BOOT low across RST -> ROM loader; sync, erase, write blocks
over J8; verify by reading back (the ESP8266 ROM may lack a flash
checksum command: check; ESP32 ROMs have one); reset with BOOT high, then
section 3 (reflashing wipes Zimodem's config; the profile and our stored
network put it back).

Sources: a file on the card or a USB stick first (found as FIRMWARE.UF2
is), an image built into our firmware later (compressed; boards with a
modem only, fine in the Frankless's 16 MB), offered when the module's
version differs. ESP32 modules: another chip family in the flasher, the
rest unchanged (still Zimodem).

## Order

1. Board header and start-up pin states (xir.15).
2. Settings record to 512 bytes, network block.
3. Modem start-up: reset, ROM capture, probe, scan, profile, join.
4. Port setting, fixed rows, the Modem panel (SSID typed).
5. DCD/DTR in telnet and the printer.
6. Status line, HWINFO, guide.
7. Scan list; static addresses.
8. Module image container; the flasher from card/USB; the built-in image.
9. ESP32 module.

## Progress (2026-11-19, branch wifi-modem, not merged)

Built and committed: xir.15 (pins), 17.1 (512-byte record, network in
page 1), WIFI_MODEM build option (PSRAM boards only), 17.3 (Port row, 8N1
/921600/RTS/CTS fixed), 17.2 (start-up + status line notice), 17.5 (DTR
hang-up, DCD carrier loss, print jobs start on DCD; local echo off while
connected), 17.4 (Modem panel), 17.6 (status line wifi / no wifi /
modem?, lit on DCD; HWINFO item 36; the guide's terminal/wifi-modem.md).
The user tried 17.2/17.3/17.5 and the echo
and notice fixes on the Frankless: work. The Modem panel's Reset modem
and Defaults are not tried on hardware yet.

17.6 tried on the Frankless (2026-11-19): the status field in both modes,
lit during a call, HWINFO's record, the panel's Network/Password. 17.7
(scan list) dropped by the user: typing the network is enough.

17.9 (Zimodem built with ours, CI, release modem/ with licenses and
sources; AT&U off in the Frankless build), 17.13 (ZIMODEM.ZMI container)
and 17.10 (Update firmware on the Modem panel, through the ESP8266 ROM
loader; host-tested against a fake ROM, not yet on hardware) done
2026-11-19 on branch zimodem-build. 17.8 deferred; 17.7 dropped.

Next: 17.11 (the image built into our firmware), 17.8 (deferred: static
address; the address mode field was left out of 17.1; static parts go
after the password in the page-1 block, ATW"ssid,pw,ip,dns,gw,subnet"),
17.12 (ESP32).
