# Zimodem: fixes to offer upstream

Four branches on our fork, [arlaneenalra/Zimodem](https://github.com/arlaneenalra/Zimodem),
each a candidate pull request to [bozimmerman/Zimodem](https://github.com/bozimmerman/Zimodem)
`master`. Below: how they were made and checked, then one section per branch, written to be
pasted as the PR's description. Nothing has been filed upstream.

## For us (not part of the PRs)

- **Base:** upstream `master` at `a534e51` (SVN r25618), tagged `upstream-svn-r25618` on the
  fork. Our working firmware is `frankless-build`, tagged `frankless-2026-11-19` at `e3932d0`
  (what dvi-feather-terminal's `lib/zimodem` points at); it is unchanged by this.
- **Branches** (each one commit on `upstream-svn-r25618`):

  | Branch | Commit | From our fork |
  |---|---|---|
  | `upstream-telnet-sb` | `8f6eb8f` | `6a1348c`, as is |
  | `upstream-update-fixes` | `2f1f541` | `fdb609a`'s `zcommand.ino` part; the overflow fixes from `181d31a`, redone on upstream's fixed update host |
  | `upstream-hw-flow` | `b2c5ae6` | `f95c277` and `d0f904a`, without the Frankless parts (fixed pins, defaults, `ATI12`) |
  | `upstream-wifi-options` | `1df7e7d` | new: what `f95c277`/`d0f904a` hard-code for the Frankless, as S64-S66; the config parser fix |

- **Independent:** every pair, and all four together, merge into `upstream-svn-r25618` without
  conflicts, in any order; each branch and the four together build (arduino-cli 1.5.1, ESP8266
  core 2.7.4, the stock configuration for a 4 MB ESP-12F) with no warnings beyond upstream's
  own (41 in the files touched, before and after). PRs can be filed, and taken, in any order.
- **Line endings:** upstream's `zimodem.ino` and `zcommand.ino` (and `README`) are CRLF, the
  other sources LF (with one CRLF line each, kept). The branches keep that, so each diff is only
  its change. (Our `frankless-build` turned `zimodem.ino` into LF: its diff against upstream
  shows the whole file. Worth undoing there some time.)
- **Tested on hardware** (an ESP-12F on the Frankless, through `frankless-build`):
  `upstream-telnet-sb` (the same code since 2026-11-17); `upstream-hw-flow`'s flow control and
  the 127-byte fill (since 2026-11-17/18, but with the build's fixed pins, not set by
  `ATS49`/`ATS51` on a stock build); `upstream-update-fixes`' wait for the body (with our update
  site; the overflow fixes are new). `upstream-wifi-options` is new code: the behavior it makes
  optional is what `frankless-build` runs, but the S registers themselves are untested, and its
  ESP32 line (`WiFi.setSleep`) has not been compiled (no ESP32 toolchain here).
- **Before filing:** a stock build of each branch on the module would close those gaps (our
  start-up would need `ATS49=13`/`ATS51=15` and RTS/CTS for `upstream-hw-flow`).
- **Upstream:** all 648 commits are Bo Zimmerman's, through git-svn; there is no CONTRIBUTING
  file, and 21 PRs so far (2 open); the PR list does not show whether closed ones were applied.
  A PR is best read as a well-described patch. Issue #187 ("Downstream slows drastically when a key is pressed",
  2026-11-15) may be the software flow control that `upstream-hw-flow` replaces: worth reading
  before filing that one.
- **Afterwards:** whatever upstream takes, `frankless-build` is rebased onto; with all four, it
  would keep only the Frankless defaults, and a stock build plus our start-up's settings could
  replace it.

---

## PR 1: Telnet: a sub-negotiation cut short no longer hangs the modem

Branch: `upstream-telnet-sb` (one commit, `zimodem/pet2asc.ino` only)

### The problem

`handleAsciiIAC()` skips a telnet sub-negotiation (`IAC SB ... IAC SE`) in a loop that ends on
`IAC SE` or on `*c >= 0` going false:

```c
char lastC=*c;
while(((lastC!=TELNET_IAC)||(*c!=TELNET_SE))&&(*c>=0))
{
  lastC=*c;
  *c=streamAvailRead(stream);
```

On the ESP8266, `char` is unsigned (xtensa-lx106), so `*c >= 0` is always true. And
`streamAvailRead()` returns a `uint8_t`, so a timeout or a closed connection (-1) arrives as
`0xFF`, which is `IAC`. A server that stalls or drops in the middle of a sub-negotiation leaves
the modem in this loop for good: it no longer answers `+++` or a hang-up, and only a reset gets
it back.

### The fix

The loop reads into an `int` and stops when the read returns -1 (it waits up to 250 ms for a
byte, as `streamAvailRead` does). `IAC IAC` inside the sub-negotiation is a data `0xFF`, not the
start of `IAC SE`. A runaway (no `IAC SE` within 1024 bytes) ends the loop as well.

### Testing

Built for an ESP-12F (ESP8266 core 2.7.4), and running in our build on one since 2026-11-17:
telnet sessions with servers that negotiate NAWS and TTYPE work as before. The hang itself
follows from the code above; a connection deliberately dropped mid-SB has not been tried.

---

## PR 2: AT&U: wait for the image before Update.writeStream(); no buffer overflows

Branch: `upstream-update-fixes` (one commit, `zimodem/zcommand.ino` only)

### Updates that failed at once

`Update.writeStream()` checks the image's first byte with `peek()` as soon as it is called, and
`WiFiClient::peek()` returns -1 while none of the body has arrived yet. A server that sends the
response's headers and its body in separate writes (Python's `http.server` does) makes every
update fail with `ERROR` straight after `Update.begin()` (`UPDATE_ERROR_MAGIC_BYTE`; the server
sees a broken pipe). `doUpdateFirmware()` now waits up to 10 s for the first bytes of the body.

### A failed update is said, and can be tried again

A failed update now prints the updater's error code and how many bytes were written, and calls
`Update.end()`, so a second `AT&U6502` does not fail in `Update.begin()` as "already running"
until the modem restarts.

### Two buffer overflows

- `AT&U=x` copied `x` into the 255-byte `buf` whatever its length: it is now taken only when it
  fits (a longer one is treated as a plain `AT&U`).
- The version (from the server's `-latest-version.txt`, up to 254 bytes, or from `AT&U=x`) was
  written into the 100-byte `firmwareName` with `sprintf`: `firmwareName` is now 300 bytes and
  written with `snprintf`.

### Testing

The wait for the body ran in our build on an ESP-12F, whose update site can be set: `AT&U6502`
against `python3 -m http.server`, which failed every time before, then updated the modem. This
branch keeps upstream's fixed update host, so it was built but not run against such a server.
The overflow fixes were built, not run.

---

## PR 3: ESP8266: hardware RTS/CTS when CTS and RTS are on GPIO13 and GPIO15

Branch: `upstream-hw-flow` (one commit: `serout.h`, `serout.ino`, `zstream.ino`, and one or two
lines in `zimodem.ino` and `zcommand.ino`)

### Why

The ESP8266's software CTS check can only stop output between bytes, so `serialOutDeque()`
writes one byte into an empty TX FIFO per pass of `loop()`. A stream to the terminal then runs
well under the line rate, and gets slower still when the modem has other work to do.

UART0 has its own flow control on GPIO13 (U0CTS) and GPIO15 (U0RTS). With RTS/CTS flow control
(`AT&K3`) and the pins set there (`ATS49=13`, `ATS51=15`), the UART now does it itself:

- **CTS:** the UART stops sending within the byte when CTS goes inactive, so `serialOutDeque()`
  fills the TX FIFO. `ZStream::loop()` also dequeues inside its socket read loop.
- **RTS:** the UART drives it from its RX FIFO (threshold 100 of 128).

On an ESP-12F wired to a terminal with RTS/CTS: 100 KB in 7-8 s at 115200 (the line rate), and
1 MB in 19-20 s at 921600 (about 53 KB/s), with no holds on either side.

### Details

- The FIFO is filled to 127 bytes, not 128: `availableForWrite()` is 128 less the FIFO's count,
  but the core's `write()` treats 127 as full and spins without yielding, so a 128th byte
  written while CTS is held ended in a soft watchdog reset about 3 s later (`epc1` in
  `uart_tx_fifo_available`).
- `serialHwFlowApply()` sets this up from `setFlowControlType()`, and again after every
  `HWSerial.begin()` (a rate or format change rewrites both UART configuration registers) and
  after the pins are set from the defaults or the saved configuration.
- The core turns on GPIO13's pull-up (for a swapped RX); it is cleared, so an undriven CTS reads
  as clear to send.
- DSR, DTR, RI or DCD on GPIO13 or GPIO15 are moved off (the ESP8266's default DSR is GPIO13).

### Nothing changes for anyone else

Any other pins keep the software flow control as it is, and so does the ESP32. The ESP8266
defaults (CTS GPIO5, RTS GPIO4, flow control off) do not use the new code: it is only on with
`ATS49=13`, `ATS51=15` and RTS/CTS.

### Testing

On an ESP-12F (core 2.7.4) with CTS/RTS wired to GPIO13/15, in our build (which fixes the pins at
13/15 rather than setting them with `ATS49`/`ATS51`): the rates above; output held by CTS for
minutes (the terminal's own menus open) resumes without a reset; after a rate change (`ATB`)
the flow control still works. The stock path, the pins set with `ATS49=13`/`ATS51=15`, has been
built but not yet run.

---

## PR 4: WiFi: power save, join and rejoin times as S64-S66; no SDK copy of the network

Branch: `upstream-wifi-options` (one commit: `zimodem.ino`, `zcommand.ino`, `zcommand.h`,
`README`)

### Three settings

Saved with `AT&W`, reset by `AT&F`, each defaulting to today's behavior:

| Register | Values | What it does |
|---|---|---|
| `ATS64=n` | 0 (default), 1 | WiFi power save. 0 is the SDK's modem sleep, as now; 1 keeps the radio awake. |
| `ATS65=n` | 1-120, default 10 | Seconds `connectWifi()` waits for the network. |
| `ATS66=n` | 0-3600, default 0 | Seconds between attempts to rejoin a lost network; 0 keeps 60 s doubling up to 30 minutes. |

Why each:

- **S64:** in modem sleep the radio dozes between the access point's beacons, and a packet it
  misses waits for a TCP retransmission. In an interactive session that shows as stalls of 1-3
  seconds; with `ATS64=1` they are gone. It costs power, so it is for mains-powered modems. The
  SDK can return to modem sleep after a (re)association, so with S64=1 the ESP8266 checks once a
  second while connected.
- **S65:** an access point that is restarting, or slow to give out an address, can take longer
  than 10 seconds; an attempt cut off at 10 s gets nowhere, and the next waits for the rejoin
  delay.
- **S66:** after a long outage the doubling delay can leave the modem off the network for half
  an hour after the access point is back.

### WiFi.persistent(false) (ESP8266)

The network's name and password are Zimodem's own (saved by `AT&W`) and passed to every
`WiFi.begin()`. In its default persistent mode the SDK also writes them into its own flash
sector on every `begin()` and `disconnect()`, and a reset or a brownout during that write can
leave a network the modem cannot join until it is entered again. `setup()` now turns persistence
off before anything touches WiFi (`loadConfig()` disconnects first).

### A config parser overflow

The three new fields go last in the config file, so older files still load. While adding them:
`parseConfigOptions()` wrote past `configArguments` when a file had more fields than the
firmware knew (a newer firmware's file, read after a downgrade): it moved on to
`CFG_LAST + 1`. It now stops at `CFG_LAST`.

### Testing

Built for the ESP-12F (core 2.7.4). The behavior S64-S66 make optional (radio awake, 30 s
joins, a fixed 30 s rejoin) has run on an ESP-12F since 2026-11-17 in our build, where it fixed
interactive stalls and reconnects after access-point outages. The ESP32 path
(`WiFi.setSleep()`) has not been built here.
