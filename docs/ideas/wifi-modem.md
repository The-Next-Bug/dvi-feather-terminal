# Plan: WiFi modem mode (the ESP-12F on J8 and J1)

Status: planned with the user, 2026-11-19; nothing built. Beads: the
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

Runs from the main loop in WiFi modem mode, without holding up the logo or
the CP/M monitor. The test's fake Zimodem (from test_printjob's) boots at
any rate, stays silent, answers late, or loops in the ROM.

1. Reset: BOOT high, RST low >= 1 ms, release.
2. ROM output: listen at 74880 for ~300 ms. The ESP8266 ROM prints its
   reset cause and boot mode there ("rst cause:2, boot mode:(3,6)"); keep
   it for the panel and HWINFO. Boot mode (1,x) means GPIO0 was low: the
   module sits in its loader and the scan would fail.
3. Probe at 921600: bare CR, then AT CR, OK on a line of its own within
   ~2 s.
4. No OK: scan 115200, 74880, 460800, 230400, 57600, 38400, 19200, 9600,
   2400, 1200. Each: UART rate, input flushed, CR, AT, ~300 ms for OK.
   (74880 from clk_peri 128 MHz: divider error well under 0.1%.)
5. Found elsewhere: Zimodem's rate command for 921600 with RTS/CTS, our
   UART switched, AT again, AT&W, so it starts at 921600 from then on.
6. Nothing: one more reset and scan, then "modem?" (SET-UP's Reset modem
   tries again).
7. Profile: query each item, write only what differs, AT&W only after a
   change (no flash wear on a normal boot). Items: result codes for our
   parsers (Q0 V1), DCD on GPIO4 (S47=4), DTR hang-up (S63=2, safe now we
   drive DTR), RTS/CTS. Terminal mode: echo off; CP/M mode: Hayes
   defaults (echo on, verbose). Command names to be checked against
   Zimodem's README.
8. Network: not joined to our stored SSID -> join with our credentials.
   Then read SSID, IP, RSSI, firmware version (ATI), ATI12's reset info.
9. Later, a modem that stops answering AT: back to 1, with a back-off.

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
