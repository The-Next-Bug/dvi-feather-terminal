# Printing

<!-- Documents: src/printer/printjob.[ch], apps/terminal/printer_glue.c,
     src/ui/printpanel.[ch], machine.c's ports 05h/06h and the BIOSes' LIST
     (src/machine/cpm/bios.asm, cpm3/bnkbios3.asm, mpm/bnkxios.mac). -->

The Z80 machine's printer (LST:) can print over WiFi, through a
[Zimodem](https://github.com/bozimmerman/Zimodem) on an ESP8266 module
(an ESP-12F, for example) wired to the board's serial port, TTY2. Each
print job gets a fresh connection: the modem's `AT+PRINT` sends it over
**IPP** to a network printer or a CUPS queue. Printers that only take raw
TCP (JetDirect, port 9100) can be reached through a CUPS queue.

This works only in CP/M mode, and only while the Printer setting is on.
While it is on, TTY2 belongs to the printer. CP/M 3's AUX:, CP/M 2.2's
RDR: and PUN:, and MP/M's console 1 then get no input, and anything they
send is dropped. With the Printer off, TTY2 is the serial port as before.
One modem cannot dial in to MP/M and print at the same time.

## Wiring and the modem

On the Frankless, the modem goes on header J8: GP20 TX to the ESP's RXD,
GP21 RX from its TXD, GP23 RTS to its GPIO5 (Zimodem's CTS), GP22 CTS from
its GPIO4 (Zimodem's RTS), and 3V3 and GND. Put 100-470 µF across the
module's supply for its transmit peaks. The modem and SET-UP must agree
on the line: 115200 8N1 and RTS/CTS are what the Frankless build of
Zimodem starts with.

On the modem, join the WiFi once and save it (`ATW"ssid,password"`, then
`AT&W`). The firmware sets everything else per job: it sends `ATE0Q0V1`,
so echo and quiet settings saved on the modem do not matter.

## The Printer panel

**P** in SET-UP opens it:

![The Printer panel](../images/generated/printpanel.png)

{{#include ../generated/settings-printer.md}}

- **Printer on TTY2**: off or IPP.
- **IPP sends**: text uses `AT+PRINTA`, which Zimodem sends as
  `text/plain`; it fixes up CR LF and wraps lines at 80 columns. Raw uses
  `AT+PRINTR`, sent as `application/octet-stream`, for PostScript and for
  text wider than 80 columns. The printer (or CUPS) has to recognize what
  raw data is.
- **Job ends after**: how long the Z80 must send nothing before the job is
  closed and goes to the printer. WordStar's background printing pauses
  between pages, so a short time can split a document into several jobs.
- **Target**: `host:port/path`, for example `192.168.1.10:631/ipp/print`
  (a CUPS queue: `host:631/printers/NAME`). Type it on the Target row;
  Backspace takes the last character off.

Return takes the choices back to SET-UP, whose Return saves them; Esc goes
back without them. The last line says how the last job went since
power-on. SET-UP's [HWINFO](tools/hwinfo.md) tool shows the same as a
Printer record.

## Printing

Print to LST: as usual:

```
A>PIP LST:=REPORT.TXT
```

In CP/M 3, `DEVICE LST:=NULL` turns the printer off until the next cold
boot, and `DEVICE LST:=LPT` turns it back on. In MP/M II, LST: is shared
by the consoles in turn.

The status line shows `PRN` while the Printer is on. It is lit while a job
is being opened, printed or ended, and reads `PRN!` after a job failed.

A job goes like this:

1. The first byte opens a job. Up to 128 bytes are kept while the modem
   answers; after that, the printer reads busy and the BIOS waits.
2. The firmware gets the modem's attention: CR, `ATE0Q0V1`, then `AT`. If
   `OK` does not come back (the modem may still be in an earlier job), it
   sends `+++` with 1.5 s of silence on each side, then `ATH`, and tries
   once more.
3. `ATS61=` the idle time, then `AT+PRINTA:` or `AT+PRINTR:` and the
   target. Zimodem answers nothing when it has connected and `ERROR` when
   it could not; the data starts 2 s after the command if no `ERROR` came.
4. The data goes straight to the modem.
5. The end, after the idle time without a byte, or after `OUT 05h,01h`
   (see [the ports](../reference/ports.md#printer-05h-06h)): the firmware
   stops taking data, and Zimodem ends the job itself once it has had
   nothing for the idle time (that is what `ATS61` set), then answers
   `OK`. If no `OK` comes, the job counts as failed (`no end`), and the
   next job gets the modem back first.

A job does not end with `+++`. Zimodem times the pause around the pluses
when it reads them, not when they arrive, so after a backlog (a printer
slow to take the data) it took them as data, and they were printed.

If the modem does not answer, the printer cannot be reached, or the
connection is lost, the job fails. The rest of its bytes are dropped and
the printer stays ready, so the Z80 never hangs on it. The next job tries
again from the start.

A line that begins with `+++` after more than a second's pause would end
a job early: that is Zimodem's escape.

## WordStar

WordStar 4 prints to LST:. Its PostScript printer, `PS`, wants **IPP
sends: raw**. A plain-text printout (`ASCII`, `XTRACT`) works with text,
as long as its lines fit 80 columns. See [WordStar 4](wordstar.md#printing).
