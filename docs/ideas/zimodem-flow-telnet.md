# Zimodem on J8: hardware flow control, throughput and telnet

Status: proposal (bd xir, 2026-11-16). Nothing here has been built or
tried. Written from a read of lib/zimodem (the user's fork,
arlaneenalra/Zimodem, branch frankless-build, fdb609a) and this
firmware's serial.c.

## What goes wrong now

Telnet through the ESP-12F on J8 starts fast, then bogs down and stays
slow. Full-screen programs (vim, WORMS) lag behind the keys. The server
always sees 80x24, even at 132 columns. The causes, all in Zimodem:

1. **One byte per loop pass.** On the ESP8266, with RTS/CTS on,
   `serialOutDeque()` (serout.ino) writes one byte, and only into an
   empty 128-byte TX FIFO. `ZStream::loop()` calls it once per Arduino
   `loop()` pass, and `enqueByte()` uses the same "FIFO empty" test. This
   is because Zimodem reads CTS in software (`digitalRead(pinCTS)`, GPIO5):
   with one byte in flight it can stop within a byte. Throughput is one
   byte per pass, and every pass also does the WiFi work, so it falls
   far below 115200 whenever the WiFi stack is busy. Behind it sit the
   4 KB `TBUF` and the TCP receive window, which is where vim's screen
   updates and the echo of the keys wait.
2. **`ATDTX` is XON/XOFF.** `T` makes the connection telnet; `X` makes it
   XON/XOFF (connSettings.ino), so Zimodem ignores our RTS and waits for
   an XOFF we never send in RTS/CTS mode (bytes can be lost when our ring
   fills), and swallows ^S/^Q typed by the user. Plain `ATDT` inherits
   RTS/CTS from the default.
3. **No window size.** `handleAsciiIAC()` (pet2asc.ino) refuses every
   option but TTYPE: `DO NAWS` gets `WONT NAWS`, so telnetd uses 80x24.
   It also refuses the server's `WILL ECHO` and `WILL SGA`, which real
   clients accept.
4. **TTYPE says "Zimodem"** (DEFAULT_TERMTYPE) unless S41 is set.
5. **Sub-negotiation parsing.** The loop
   `while(((lastC!=TELNET_IAC)||(*c!=TELNET_SE))&&(*c>=0))` compares a
   `char`: if `char` is signed on xtensa-lx106, it stops at the first byte
   >= 0x80 (the IAC) and the SE byte (0xF0) is printed as data. Check:
   `xtensa-lx106-elf-gcc -dM -E - </dev/null | grep CHAR_UNSIGNED`, and look
   for a stray 0xF0 glyph just after connecting. Checked (bd xir.8): char
   is unsigned there, so that does not happen; the real bug was that the
   loop never saw the end of the data (`*c>=0` always true, a timeout's -1
   read as 0xFF = IAC), so an SB cut short hung Zimodem for good. Fixed in
   pet2asc.ino: ints, stop on -1, IAC IAC as data, 1024-byte cap.
6. **`streamAvailRead()`** waits with `delay(1)` up to 250 ms for each byte
   of a telnet command split across TCP segments, stalling the stream.
7. **Zimodem never drives its RTS** on the ESP8266 (the overflow code in
   wificlientnode.ino is commented out), so a fast paste from the terminal
   can overrun it.

This firmware's side is sound: serial.c stops draining the UART FIFO when
the 2 KB ring has under 64 bytes free, so the PL011 drops RTS with 16
bytes of FIFO headroom (RX watermark at half the 32-byte FIFO). The status
line now shows it (bd xir.3): `RTS` lit while we hold the modem off, `CTS`
lit while the modem holds us.

## Rewiring J8 for the ESP8266's hardware RTS/CTS

The ESP8266's UART0 has hardware flow control, but only on fixed pins:
**U0CTS = GPIO13 (MTCK)** and **U0RTS = GPIO15 (MTDO)**, pin function 4
(`FUNCTION_4` in the Arduino core). Today's wiring (lib/zimodem
BUILDING.md) puts them on GPIO5 and GPIO4, which only software can watch.

| J8 (UART1, 3.3 V) | Now | Proposed |
|---|---|---|
| TX GP20 | ESP RXD (GPIO3) | unchanged |
| RX GP21 | ESP TXD (GPIO1) | unchanged |
| RTS GP23 (terminal's output) | ESP GPIO5 (software CTS) | **ESP GPIO13 (U0CTS)** |
| CTS GP22 (terminal's input) | ESP GPIO4 (software RTS) | **ESP GPIO15 (U0RTS)** |
| 3V3, GND | module power | unchanged, 100-470 uF at the module |

Things to watch:

- **GPIO15 is a boot strap**: it must be low at reset for the ESP to boot
  from flash. Bare ESP-12F modules need an external 10 kOhm pull-down
  (most carrier boards have one; check). Our GP22 is an input with the
  RP2350's default pull-down (serial.c leaves it at GPIO_FUNC_NULL when
  Flow control is not RTS/CTS), so it does not fight the strap. Do not add
  a pull-up on that net. While the ESP boots, GPIO15 low reads as "clear to
  send" to the terminal. That is harmless: the boot ROM's 74880-baud output
  on TXD is the only noise, as now.
- **GPIO13 is Zimodem's default DSR pin** on the ESP8266
  (DEFAULT_PIN_DSR 13). The Frankless build must move DSR off it (-1),
  which it can, since DSR/DTR/DCD/RI are unconnected on J8 anyway.
- Both ends use the usual active-low RTS/CTS, so no inversion
  (Zimodem's S-register pin polarities stay at their defaults).
- GPIO4/GPIO5 become free (they are I2C-capable, for later).
- Keep the old wiring working: the firmware change below is a build or
  S-register choice, not a replacement.

## Zimodem changes (lib/zimodem, branch frankless-build)

Constraint: Zimodem has to stay usable on far less capable hardware (a
C64 at 1200-2400 baud, an ESP-01 with no handshake lines, BBS use), so
every change is either the Frankless build's default or a new S
register, and the stock behavior stays the stock default. The parts that
let the terminal's characteristics through (size, type) are opt-in.

- **Z1a, hardware flow control (after the rewire)**: built (bd xir.7,
  serout.ino serialHwFlowApply: on whenever RTS/CTS and S49=13 / S51=15;
  the Frankless build fixes those pins (FRANKLESS_HW_FLOW, default 1:
  saved S49/S51 ignored, others refused; 0 for the old wiring); DSR off; the pull-up
  the core puts on GPIO13 cleared, so an undriven terminal RTS reads as
  clear to send; reapplied after HWSerial.begin, which rewrites both
  config registers). Untried on hardware as of 2026-11-17. The sketch: in
  `ZSerial::setFlowControlType()` for the ESP8266, with FCT_RTSCTS and the
  pins on 13/15: `pinMode(13, FUNCTION_4)`, `pinMode(15, FUNCTION_4)`,
  `USC0(0) |= 1 << UCTXHFE` (the UART stops sending when CTS goes high),
  `USC1(0) |= (1 << UCRXHFE) | (threshold << UCRXHFT)` (RTS goes high when
  the RX FIFO passes the threshold, ~100 of 128). Then `serialOutDeque()`
  may fill the FIFO (`while (availableForWrite() > 0)`), and
  `isSerialOut()` need not read the pin. A build flag (the Frankless
  build: on) or an S register picks it, since other boards wire CTS
  elsewhere.
- **Z1b, without the rewire**: write up to K bytes per call while the
  software CTS is active, instead of one into an empty FIFO. K is an
  S register, default 1 (stock), Frankless 12. It must stay below our
  16 bytes of FIFO headroom after RTS drops: the ESP looks at CTS once per
  batch, and what is in its FIFO is sent regardless.
- **Z1c, both**: call `serialOutDeque()` inside the socket read loop of
  `ZStream::loop()` too, not once per pass. Built (bd xir.7). Z1b, the
  batched software deque for the old wiring, was not.
- **Z2, window size (NAWS)**: answer `DO NAWS` with `WILL NAWS` and
  `IAC SB NAWS w1 w0 h1 h0 IAC SE`, the size from a new saved setting
  (S register or `AT+NAWS=132,24`, saved by AT&W). Unset: `WONT`, as now.
  Optional, opt-in: measure the terminal at connect time the way xterm's
  `resize` does (save the cursor, `ESC[999;999H`, `ESC[6n`, restore; read
  the CPR from the serial input, with a timeout). Our terminal answers
  CPR in all personalities but ADM-3A. A size change mid-call is not seen
  either way; `eval $(resize)` on the server covers that.
- **Z3, telnet clean-up**: accept `WILL ECHO` and `WILL SGA` (behind an S
  register, as BBS users may rely on today's local echo behavior); fix the
  signed-`char` SB loop; replace `streamAvailRead()`'s busy-wait with a
  small parser state kept across `loop()` passes; Frankless default
  TTYPE `vt100` (S41).
- **Z4, counters for measuring**: dropped (bd xir.6, 2026-11-17). Z1 is
  measured from the terminal instead: a `cat` with the debug serial
  status line (bd xir.4) and the raw-TCP test below.

## Workarounds until then

- Dial `ATDT`, not `ATDTX`, with the terminal on RTS/CTS.
- `ATS41=vt100` then `AT&W`.
- After connecting, or after changing columns: `eval $(resize)` or
  `stty cols 132 rows 24` on the server.

## Measuring

1. On the server: `time cat` a ~100 KB file, or
   `dd if=/dev/zero bs=1k count=100 | tr '\0' x`; at 115200 about 9 s.
2. The same from a PC's USB serial adapter straight into J8 (or
   tools/hostbridge.py): if that is smooth, the terminal is not the limit.
3. Raw TCP without telnet: `nc -l 2323 < file` on the server, dial without
   the `T` modifier. If that is as slow, it is Z1, not telnet.
4. The negotiation: `tcpdump -X port 23` on the server, or Zimodem's
   `AT&O1` log; `echo $TERM; stty size` after logging in.
5. The status line: `RTS` lit means this terminal is the bottleneck,
   `CTS` lit means the modem holds us (with Z1a, its RX FIFO is filling).
   Neither lit while output crawls means the time is lost upstream.
   Status line "debug serial" (SET-UP) gives the numbers: rx bytes/s
   against 115200's ~11.5 KB/s, the RX ring's peak, the hold counts.

## Idea: telnet in the terminal

Built (bd xir.10): see the guide's terminal/telnet.md. It differs from
the sketch below in two choices the user made: Telnet is off / auto / on,
and the dial is F11 (the Telnet panel), which turns telnet on for that
call; the negotiation is forgotten only on RIS, a setting change or the
hang-up, not on NO CARRIER.

Instead of making Zimodem a full telnet client, the terminal could do it:
dial raw TCP through Zimodem (no `T` modifier, so Zimodem passes bytes
straight through) and handle telnet in the terminal, which knows what
Zimodem cannot:

- a portable `src/term/telnet.[ch]` (host-tested) in front of the
  personalities' parsers: IAC IAC, DO/DONT/WILL/WONT, SB ... SE;
- NAWS from the real width (80 or 132) and 24 rows, sent again when
  DECCOLM or SET-UP changes the columns, so the server gets SIGWINCH;
- TTYPE from the personality: `vt100`, `vt52`, `adm3a`;
- accept ECHO and SGA, refuse the rest; on the way out, 0xFF doubled and
  CR sent as CR NUL (or CR LF per the Newline setting);
- a SET-UP setting (off by default: a plain serial host may send 0xFF in
  8-bit data, though UTF-8 never does) and perhaps later a small dialer
  panel that sends `ATD"host:port"` from a list.

This keeps Zimodem simple (good for the lesser hardware) and passes the
terminal's size and type through cleanly. It does nothing for the
throughput: Z1 is needed either way. In CP/M mode the Z80's programs see
SIO2 raw, so this would be terminal mode only.
