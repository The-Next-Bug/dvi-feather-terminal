# Telnet

The terminal can speak telnet itself to a host reached over the network
through a WiFi modem on its serial line, such as a
[Zimodem](https://github.com/bozimmerman/Zimodem) on an ESP8266. The modem
then only carries bytes (a raw TCP connection), and the terminal tells the
server what the modem cannot know: its real width (80 or 132 columns, and
again whenever it changes, so full-screen programs redraw for the new
width) and its terminal type.

Telnet is for terminal mode. In CP/M mode the serial line belongs to the
Z80 (its second serial port and the printer), and **F11** is the
machine's reset key.

*Sources: `src/term/telnet.c` (the protocol), `src/ui/telnetpanel.c` (the
panel), `apps/terminal/telnet_glue.c` (the dial, the hang-up, the panel's
glue).*

## Dialing: F11

**F11** in terminal mode opens SET-UP's Telnet panel at its Host row (F11
sends nothing to the host in terminal mode):

![The Telnet panel](../images/generated/telnetpanel.png)

Type the host, a name or an address, with `:port` if it is not 23, and
press **Return**. The terminal saves the panel's choices (as SET-UP's
Return does), leaves SET-UP and sends `ATD"host:port"` to the modem, which
has to be at its AT prompt. The modem's answer (`CONNECT`, `NO ANSWER`, ...)
shows on the screen as usual. Without the `T` modifier Zimodem makes a raw
TCP connection, so its own telnet handling stays out of the way.

For that call, telnet is on, whatever the Telnet setting says.

**F11** during the call opens the panel at its Call row: **Return** hangs
up. The terminal waits until the line has been quiet for 1.5 s, sends
`+++`, waits another 1.5 s and sends `ATH` (Zimodem's escape needs its
guard time on both sides: keys typed meanwhile are dropped). The status
line shows `hangup` until it is done.

## The Telnet panel

**N** in SET-UP (terminal mode) opens the panel at its first row.

{{#include ../generated/settings-telnet.md}}

- **Telnet**: what the terminal does with the host's bytes outside a call
  dialed with F11.
  - **off**: they reach the screen as they come.
  - **auto**: telnet from the first telnet command the host sends (a
    telnet server always starts by negotiating), until RIS. Before that an
    `FFh` byte and the one after it are shown as they are, so a plain
    serial host is not affected unless it sends `FFh` followed by `FBh` to
    `FEh` or `FAh`.
  - **on**: every `FFh` from the host starts a telnet command.
- **Terminal type**: the name sent when the server asks (it becomes `TERM`
  there). Empty: the [Terminal type](terminal-types.md) in use, `VT100`,
  `VT52` or `ADM3A`. Type another (no spaces), for example `vt102`, which
  tells full-screen programs they may insert and delete lines.
- **Host**: the last host dialed.
- **Call**: the call dialed with F11, if one is up.

Return on Telnet or Terminal type takes the choices back to SET-UP, whose
Return saves them (opened with F11, Return saves at once). Esc goes back
without them.

## What the terminal negotiates

It sends nothing until the server negotiates, then:

| Option | Terminal's answer |
|---|---|
| NAWS (window size) | `WILL`, then the width and 24 rows; the size again whenever the width changes |
| TTYPE (terminal type) | `WILL`, then the Terminal type's name when asked |
| SGA (suppress go-ahead) | accepted both ways |
| ECHO (the server echoes) | accepted |
| anything else | refused |

An `FFh` the terminal sends (it never does from the keyboard) goes out
doubled, as telnet wants; Return goes out as a bare CR. The negotiation is
forgotten on RIS (`ESC c`), when the Telnet setting changes, or when a call
dialed with F11 is hung up. A server that ends the call by itself leaves
the terminal in telnet until one of those: the window size updates it
then sends would reach the modem's AT prompt (which answers `ERROR`).

## The status line

`telnet` on the [status line](status-line.md) while telnet is on or auto,
lit once the server has negotiated; `hangup` while a hang-up is under way.

## Zimodem notes

- Dial with the terminal's F11 rather than `ATDT`: `ATDT` makes Zimodem
  do telnet itself, and it refuses the window size, so the server sees
  80x24.
- Use the same flow control on both sides: the Frankless build of Zimodem
  defaults to RTS/CTS, as SET-UP's Flow control should be. `X` in a dial
  command (`ATDTX`) switches Zimodem to XON/XOFF for that call.
- Throughput through an ESP8266 Zimodem is limited by the modem. Wired
  the earlier way (its CTS on GPIO5) it sends one byte per pass of its
  main loop; on GPIO13/GPIO15, the ESP's own RTS/CTS pins, the Frankless
  build fills the UART instead ([wiring](../cpm/printing.md#wiring-and-the-modem)).
  The flow control field on the status line shows which side holds the
  other off, and the debug serial status line the bytes per second.
- Faster than 115200: Zimodem's `ATB230400` (up to `ATB921600`) switches
  the modem right after its OK; then set the same Baud rate in SET-UP and,
  once both talk, `AT&W` on the modem. Only with RTS/CTS on both sides.
