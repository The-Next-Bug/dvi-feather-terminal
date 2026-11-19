# Idea: Espressif's ESP-AT firmware on the ESP-12F instead of Zimodem

Status: future work (user, 2026-11-18), nothing built; bd xir.16. Written
from the v2.3.0.0_esp8266 release page (github.com/espressif/esp-at) and
the release-v2.2.0.0_esp8266 docs (hardware connection, TCP/IP and basic
AT commands, compiling, AT port pins, user-defined commands) and
factory_param_data.csv on that branch. The v2.3 docs were not read; check
anything below against them before building.

## Summary

ESP-AT works on the ESP-12F, but it does not drop in for Zimodem. Zimodem
is a Hayes modem: the far end of the serial line dials, answers, prints
and hangs up by itself, and our firmware (telnet_glue.c, printjob.c) just
sends it a few AT commands. ESP-AT is a network stack that a host program
drives command by command. Switching moves the modem's logic, and all of
its configuration, into the dvi-feather firmware. Worth it only as a
second modem type beside Zimodem, after the bench test below shows the
speed is there.

## The release

v2.3.0.0_esp8266 is a minor update to v2.2.2.0: ESP8266_RTOS_SDK moved to
commit c965e03d and mbedtls from 2.16.5 to a commit just after 3.6.3 (a
breaking change for servers that do not speak it), `AT+CIPSSLCCIPHER`
added, a passthrough-mode SSL reconnect bug fixed. Nothing on the page
about how long the ESP8266 stays supported.

## Gains

- Throughput, probably: an RTOS with an interrupt-driven UART driver
  rather than Zimodem's one byte per loop() (~53 KB/s at 921600, bd
  xir.12). Not measured. Baud up to 4500000; `AT+UART_CUR=<baud>,8,1,0,3`
  turns on RTS and CTS (`_DEF` saves it).
- Connection events in band: in normal (not passthrough) mode, received
  data arrives as `+IPD,<len>:<data>` (`+IPD,<id>,<len>:` with
  `AT+CIPMUX=1`), and the far end closing gives `CLOSED`. That is Option B
  of modem-carrier.md without writing ESP firmware, and carrier loss
  without the J1 wires (bd xir.15). `AT+CIPDINFO=1` adds the remote
  address.
- Several connections at once (`AT+CIPMUX=1`): a print job and a telnet
  session on one module, which covers second-modem.md.
- TLS with a current mbedtls, MQTT, HTTP client, SNTP; maintained by
  Espressif.
- Our own AT commands: `esp_at_cmd_struct` handlers (test/query/set/
  execute), registered with `esp_at_custom_cmd_array_regist()` from
  `at_custom_init()`.

## Losses

- Hayes behavior. CP/M programs on SIO2 that dial with `ATDT`, answer on
  RING or set S-registers stop working, and ESP-AT's `+++` needs only
  20 ms guards (ours: 1.5 s). Keeping them means a Hayes layer in our
  firmware (below).
- The printer: printjob.c uses Zimodem's `AT+PRINT` (IPP). We would need
  our own IPP client (HTTP POST of a print job) over an ESP-AT connection.
- Passthrough mode (`AT+CIPMODE=1`, `AT+CIPSEND`) is a poor fit for a
  terminal: when the TCP connection drops it reconnects to the same host
  by itself (`AT+CIPRECONNINTV`, default 100 ms) and says `CLOSED` only
  with `AT+SYSMSG` bit 2 set. So a terminal uses normal mode, and every
  send is `AT+CIPSEND=<len>`, wait for `>`, the data, wait for `SEND OK`.
  That is a driver, not a byte pipe, and where the extra speed could go.
- Our Zimodem work: the hardware flow fix (xir.7, xir.13), ATI12, AT&U
  updates from our own site, the AT+CONFIG workflow. `AT+CIUPDATE`
  updates from Espressif's server only.
- The AT core (components/at/lib) is a closed binary: commands can be
  added around it, bugs inside it cannot be fixed as serout.ino was.
- A second ESP toolchain: ESP8266_RTOS_SDK v3.4 and esp-at's `build.py`,
  not arduino-cli.

## Wiring

ESP-AT's default AT port is UART0 swapped: RX GPIO13, TX GPIO15, CTS
GPIO3, RTS GPIO1 (log on UART1, GPIO2). J8 is wired for Zimodem the other
way round: ESP RX GPIO3, TX GPIO1, CTS GPIO13, RTS GPIO15, and the
RP2350's UART1 pins (GP20-23) have fixed roles. A custom build changes the
module's row in
components/customized_partitions/raw_data/factory_param/factory_param_data.csv:
uart_tx_pin 1, uart_rx_pin 3, uart_cts_pin 13, uart_rts_pin 15,
tx_control_pin -1 (the stock WROOM-02 row drives GPIO5 high to switch
GPIO15). The docs allow TX/RX on 1/3 or 15/13; whether CTS 13 / RTS 15
work with the unswapped pair is not stated (they are UART0's own flow
pins, so expected to). Then build/customized_partitions/factory_param.bin.
The log stays on GPIO2, which must still be high at reset.

The J1 lines of bd xir.15 still matter: RST to recover a module at an
unknown baud rate, BOOT (GPIO0) to flash it from our side (below). DCD and
DTR do not: ESP-AT has no modem lines, `CLOSED` and `AT+CIPCLOSE` replace
them.

## What our firmware would need

### The driver

A modem module (portable, host-tested against a fake ESP-AT as
test_printjob does against a fake Zimodem; cold except the receive path):

- A response parser: `OK`, `ERROR`, `SEND OK`, `SEND FAIL`, `>`, `busy p...`,
  `WIFI CONNECTED`, `WIFI GOT IP`, `WIFI DISCONNECT`, `<id>,CONNECT`,
  `<id>,CLOSED`, and `+IPD` with its length-counted binary payload, which
  can arrive between a command and its answer.
- A sender: our TX queue cut into `AT+CIPSEND` chunks (up to 2048 bytes;
  passthrough's limits are 1460 out / 2048 in), one in flight, the next
  only after `SEND OK`. Keys typed meanwhile wait.
- Flow: RTS/CTS stays on (the UART's), so serial.c is unchanged; the
  parser runs where telnet_glue_filter does today.
- telnet_glue.c over it: dial = `AT+CIPSTART="TCP","host",port`, hang-up =
  `AT+CIPCLOSE` (no 3 s guard dance), `CLOSED` = the far end went (status
  line, telnet state forgotten). The telnet filter is unchanged.
- printjob.c: a second back end, IPP over its own link ID.

### A Hayes layer for CP/M (optional)

So that SIO2 programs keep working: cpm_glue's SIO2 hooks talk to a small
Hayes emulator (ATDT"host:port" / ATDThost, ATA, ATH, ATZ, ATE/Q/V, S0
and a few registers, `+++` with guard times, CONNECT / NO CARRIER / RING)
that drives the same driver. Without it, CP/M sees ESP-AT raw, which is
usable only by programs written for it.

### Configuration

Zimodem keeps all of this itself (AT+CONFIG, /zconfig_v2.txt). With
ESP-AT it becomes ours, which is most of the work:

- Modem type: a setting (none / Zimodem / ESP-AT), or found at start by
  asking (`AT+GMR` answers with the AT version; Zimodem's `ATI` its own).
  None keeps SIO2 and terminal mode a plain serial line, as today.
- Baud: the module boots at its saved `AT+UART_DEF` rate (factory
  115200). We set the rate we want and save it; after a mismatch (a module
  swapped in, a rate changed elsewhere) we try the likely rates with `AT`,
  and as a last resort pulse RST (J1) and use 115200 before the stored
  rate applies, if the build keeps it at boot (check).
- WiFi: station mode (`AT+CWMODE=1`), a scan list (`AT+CWLAP`: SSID,
  RSSI, security) to pick from, the password typed (masked), join
  (`AT+CWJAP`, its error codes shown: timeout, wrong password, AP not
  found), auto-connect at power on (`AT+CWAUTOCONN=1`), status
  (`AT+CWJAP?`: SSID, channel, RSSI; `AT+CIPSTA?`: IP, gateway, mask).
- Where the credentials live: in the module (`AT+SYSSTORE=1`, ESP-AT's
  NVS), not in our settings flash. Our settings record would hold a
  plain-text password readable from a flash dump (picotool), and the
  module already has a place for it. Cost: a swapped module needs joining
  again. Zimodem does the same today.
- IP: DHCP by default; static address, gateway, mask (`AT+CIPSTA`) and
  DNS (`AT+CIPDNS`) as options; a hostname (`AT+CWHOSTNAME`).
- Time: SNTP (`AT+CIPSNTPCFG`, `AT+CIPSNTPTIME?`) could set the clock on
  boards without an RTC (the Sock) or correct the DS3231 (rtc_set) once a
  day, with a server and a UTC offset setting. A new feature, not a
  replacement; also possible with Zimodem's `AT+TIME`-style commands
  (check).
- Hosts: telnet_host is one text; a short list of named hosts (a phone
  book: name, host, port, telnet on/off) would be the natural next step
  either way. Text blocks in the settings record are tight
  (SETTINGS_TEXT_OFFSET, SETTINGS_TEXT2_OFFSET); a list wants a file on
  the card or a third block.
- TLS: on/off per host; CA certificates go into the module's partitions at
  build time (esp-at's customized_partitions), so no certificate handling
  on our side.
- The printer target (printer_target) stays as it is; the IPP client
  turns it into a connection and a POST.
- Diagnostics: firmware version (`AT+GMR`), RSSI and IP on the panel,
  the last error, and HWINFO's modem record (a new item: type, version,
  IP).

SET-UP would get a Network panel (W is free: SET-UP uses C, D, M, N, P,
R, T, U): Modem type, Baud, Network (Return: the scan list, then the
password prompt), Status (SSID, RSSI, IP), Address (DHCP / static, then
its fields), Hostname, Time server, Test (ping `AT+PING`, or a connect to
the telnet host). Its work runs from the main loop, as drives_work and
clock_service do, since joins take seconds. Like clockpanel.c and
telnetpanel.c it is portable (test_setup) and cold.

### Updating the module

ESP-AT's OTA is Espressif's server only. Options: a custom AT command
taking an image over the UART (writes the OTA partition, then boots it);
or, with BOOT and RST on J1, our firmware puts the ESP8266 in its ROM
loader and flashes it from a file on the card (the ROM's SLIP protocol, as
esptool does). The second works for Zimodem too and makes a bricked
module recoverable without a USB adapter, so it may be worth doing on its
own (after xir.15).

## Resources

The SRAM-only boards have little room (Adalogger Feather ~3.7 KB to the
limit, CLAUDE.md's budget), so everything but the receive parser must be
cold, and the Network panel and Hayes layer could be left out of boards
without a modem (a board header setting, as BOARD_HAS_RTSCTS). The
Frankless has ~97 KB free. Flash: the ESP-12F's 4 MB is plenty (ESP-AT's
default image is 2 MB).

## First step: a bench test, no firmware changes

1. Build ESP-AT for the ESP8266 with the factory_param row above, flash a
   spare ESP-12F (USB adapter), wire it to J8 as Zimodem's.
2. Terminal mode at 115200, flow control none: type `AT`, `AT+GMR`,
   `AT+CWJAP="ssid","pass"`, `AT+UART_CUR=921600,8,1,0,3`.
3. SET-UP: 921600, RTS/CTS. `AT+CIPSTART="TCP","host",23`,
   `AT+CIPMODE=1`, `AT+CIPSEND`, then `cat` a 1 MB file on the host and
   read the debug serial status line's rates and holds (xir.4), as done
   for Zimodem (~53 KB/s, xir.12).
4. `+++` (20 ms guards), `AT+CIPMODE=0`, connect again and watch `+IPD`
   and `CLOSED` when the host ends the session.

If passthrough is not much faster than Zimodem, stop there: normal mode
can only be slower.

## Newer modules (and SSH)

Asked 2026-11-19: a more capable module of about the ESP-12F's size,
mainly for SSH. Finding: Zimodem already has an SSH client
(lib/zimodem/zimodem/wifisshclient.ino, libssh2 over mbedtls), on for
every ESP32 build (`INCLUDE_SSH`, #undef'd only for the ESP8266), dialed
`ATDS"user:pass@host:port"` (port 22 by default with S and a user;
README). zimodem.ino has pin sets for ESP32-C3 and ESP32-S3 dev boards.
ESP-AT has no SSH, so on these chips Zimodem stays the better fit and the
rest of this note becomes moot for SSH.

| Chip | Modules (sizes approximate, check datasheets) | Notes | Zimodem |
|---|---|---|---|
| ESP32-C3 | Ai-Thinker ESP-C3-12F (ESP-12F outline, 16x24 mm, different pinout and straps); ESP32-C3-MINI-1 (~13x17); C3-WROOM-02 (18x20) | RISC-V 160 MHz, 400 KB SRAM, WiFi 4, BLE 5, USB serial/JTAG for flashing | ARDUINO_ESP32C3_DEV config |
| ESP32-S3 | S3-MINI-1 (~15x21); S3-WROOM-1 (18x26), PSRAM options (N8R2...) | 2 x 240 MHz, 512 KB SRAM + PSRAM, crypto acceleration: most SSH headroom | ARDUINO_ESP32S3_DEV config |
| ESP32 | WROOM-32E (18x26) | 2 x 240 MHz, 520 KB | Zimodem's main ESP32 target, most tested |
| ESP32-C6 | C6-MINI-1, C6-WROOM-1 | WiFi 6 (2.4 GHz), BLE, Thread | no config; needs a newer Arduino core: porting risk |
| ESP32-C5 | C5-WROOM-1 | dual-band 2.4/5 GHz WiFi 6 | newest, untested |

Passed over: the RM2 (CYW43439, Pico W's radio: no firmware of its own,
the RP2350 would run WiFi and SSH), WizFi360 (AT only), Realtek BW16
(dual-band, no Zimodem).

Pick: ESP32-S3 (MINI-1, or WROOM-1 with PSRAM if it fits the case) when
SSH matters most; ESP32-C3 when size and a near-ESP-12F outline matter
more (SSH should fit in 400 KB, with less headroom).

Hardware differences from the ESP-12F:

- UART through the GPIO matrix: RTS/CTS on any pins, so J8's wiring
  stays and the Frankless config names the pins. Our ESP8266 flow fix
  (xir.7, xir.13: serout.ino's FIFO fill, the soft WDT) does not apply;
  the ESP32 builds use the IDF UART driver, which needs its own test with
  CTS held.
- Boot straps: C3 GPIO2 and GPIO8 high at reset, GPIO9 low for the
  loader; S3 GPIO0, GPIO45, GPIO46. J1's RST and BOOT lines (xir.15) go
  to EN/CHIP_PU and the chip's boot pin instead of GPIO0.
- Power: TX peaks of ~350-500 mA, a little above the ESP8266; check the
  Frankless's 3V3 regulator and put bulk capacitance at the module.
- Our INCLUDE_FRANKLESS block (ATI12, persistent(false), the J8 pins and
  flow defaults) is ESP8266 code in places; an ESP32 version is needed.

Our side for SSH: the telnet panel dials `ATDS"user:pass@host:port"`
instead of `ATD"host"`, the telnet filter stays off for such calls, and
the password is asked for at dial time rather than kept in the settings
record. Check wifisshclient.ino for key authentication and host-key
checking first: the dial string suggests password only.

## Open questions

- Is CTS 13 / RTS 15 with TX 1 / RX 3 accepted by the AT core?
- Does `CLOSED` come in active normal mode without `AT+SYSMSG`? (Docs
  say so only for passive mode and passthrough.)
- Normal mode's throughput with one `AT+CIPSEND` in flight; does
  `AT+CIPSENDEX` or a larger chunk help?
- Does the module come up at `AT+UART_DEF`'s rate after a reset, and what
  does it print at boot on the AT port?
- How long does Espressif keep the ESP8266 line going? The ESP32-C3 (the same
  AT firmware, maintained; Ai-Thinker's ESP-C3-12F claims the ESP-12F's
  footprint, check) is the obvious next module if this goes ahead.
