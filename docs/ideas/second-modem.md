# Idea: a second Zimodem for printing while a caller is on MP/M

Status: deferred by the user (2026-11-15), not planned, no bd ticket.
Written from a read of the firmware (v0.14.1 + main) and the user's
Zimodem fork (../Zimodem, commit 181d31a: the Frankless build, 115200
baud, RTS/CTS, the AT&U update site; BUILDING.md has the J8 wiring).
Nothing has been built or tried.

## The problem

The Frankless's ESP-12F with Zimodem sits on J8, UART1, the board's
terminal UART: the Z80's SIO2 (ports 2/3). That is MP/M II's console 1,
CP/M 3's TTY2 (AUX:) and CP/M 2.2's READER/PUNCH. A call in to MP/M
(Zimodem listener: `ATE0Q1S0=1S41=1S60=1`, `ATAT23`, `AT&W`) holds the
modem in stream mode for the whole call.

Printing through Zimodem's AT+PRINT (IPP) also takes over the serial
stream until its timeout (ATS61, which the firmware sets to the job's
idle time). One ESP8266 carries
one data stream at a time, so printing from WordStar on console 0 while
someone is called in on console 1 cannot share the module. The planned
printer port (TTY2 as a printer, set in SET-UP, bd wt6: IPP only, raw TCP
was tried and dropped) gives the UART to one or the other, not both at
once.

## The idea

A second ESP-12F with Zimodem, used only as the printer:

- Wire it to UART0 on the Frankless's J1 header: GP28 TX, GP29 RX (both
  free; bd qtq noted UART0 there as a second serial port). UART0's CTS/RTS
  pins (GP2/3, 14/15, 18/19) are all taken, so no hardware flow control:
  Zimodem ATF1 (XON/XOFF) instead. Printer data (text, PostScript from
  WordStar's WSPS driver) has no ^Q/^S in it. A PIO UART on GP24-27 could
  have RTS/CTS if XON/XOFF is not enough.
- Firmware: serial.c is one UART today (the terminal's). It would need a
  second instance (its own RX ring and TX queue, XON/XOFF via
  src/common/xonxoff.c, which is already portable), and the printer
  platform hook (machine.c's ports 5/6, printer_out) pointed at it
  instead of at TTY2. Board header: optional BOARD_PRINTER_UART +
  _TX/_RX pins, counted in board.h's used-once check; HWINFO a record.
- The print job logic (src/printer/printjob.c: the attention, AT+PRINT,
  Zimodem's S61 timeout ending the job) is the same as for the
  one-modem printer port, only on another UART: build that one portable
  and port-agnostic so this is mostly board and serial work.
- BIOSes: unchanged from the printer port work (LIST on ports 5/6 in all
  three systems).

## Alternatives looked at

- One ESP32 with Zimodem: three UARTs, but Zimodem drives one serial
  port; serving two would be a large change to Zimodem itself.
- Taking turns on one modem: spool print jobs (SD card or PSRAM) and
  send them only when no call is up. Needs to know the call state, e.g.
  Zimodem's DCD (default ESP GPIO2, a boot strap pin; S46/S47) wired to a
  free RP2350 pin (J1 GP24-27). Works, but a job waits for the caller to
  hang up.

## Zimodem notes (../Zimodem/zimodem/zprint.ino)

- AT+PRINT?:host:port/path, ? = A ASCII (text/plain), R raw
  (application/octet-stream), P PETSCII. A bare AT+PRINT reuses the
  last spec, which AT&W saves.
- ASCII mode fixes up CR/LF and wraps lines at 80 columns, which breaks
  132-column output: use R for wide reports and PostScript.
- ATS61 (the job's idle timeout) is saved by AT&W but not loaded back
  (zcommand.ino, the CFG_PRINTDELAYMS line is commented out): set it
  before each job.
- +++ is timed as Zimodem reads the bytes: after a backlog it follows the
  data too closely and is passed on as data (it printed). The firmware's
  jobs end by S61 instead.
