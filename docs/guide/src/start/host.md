# Connecting a host

In terminal mode the board is a serial terminal: what the host sends is
shown, and keys typed on the USB keyboard go to the host.

## Wiring

Use a 3.3 V USB-serial adapter (not RS-232 levels):

- the board's **TX** to the adapter's **RX**,
- the board's **RX** to the adapter's **TX**,
- **GND** to **GND**,
- for RTS/CTS flow control also the board's **RTS** to the adapter's
  **CTS** and the board's **CTS** to the adapter's **RTS**.

Which pins those are depends on the board: see [Boards](boards.md).

## Line settings

115200 baud, 8 data bits, no parity, 1 stop bit (8N1) until changed in
[SET-UP](../terminal/setup.md): baud rates 300 to 115200, framings 8N1,
8E1, 8O1, 8N2, 7E1, 7O1, 7N1 and 7N2. A new rate applies as soon as SET-UP
saves it, so switch the host to match.

On a Linux host, for example:

```sh
picocom -b 115200 /dev/ttyUSB0
# or
screen /dev/ttyUSB0 115200
```

Tell programs on the host what the terminal is with `TERM`: `vt100`
(no colors), `xterm-color` (8 colors), `xterm-16color` or `xterm-256color`.
The xterm entries use the [alternate screen](../reference/vt100.md#modes),
which the terminal supports.

## Flow control

SET-UP's **Flow control**: `none` (the default), `RTS/CTS` or `XON/XOFF`,
applied when SET-UP saves, in both modes (in CP/M mode it covers the Z80's
second serial port). Without it, at 115200 baud the terminal keeps up with
ordinary output, but a long burst that scrolls the whole screen can
overrun it: the 2 KB receive buffer fills and characters go missing. And
nothing stops the host while SET-UP is open.

**RTS/CTS** (hardware):

1. Wire CTS and RTS as above.
2. Set Flow control to `RTS/CTS` and save.
3. Enable it on the host too: `stty -F /dev/ttyUSB0 crtscts`, or
   `tools/hostbridge.py --rtscts ...`.

The terminal drops RTS when its receive buffer is nearly full and raises
it once half of it is free; it sends only while CTS is asserted. Do not
choose it without the wires: an unconnected CTS floats, and if it reads
as "hold" the terminal never sends its keys or replies. Boards without the
pins (the Adalogger Feathers) do not offer it.

**XON/XOFF** (software, no wires): the terminal sends XOFF (`^S`) when its
receive buffer is half full (and again every 256 bytes if the host keeps
sending) and XON (`^Q`) once it has drained to 256 bytes; it stops sending
when the host sends XOFF and goes on at XON. Those two bytes are flow
control both ways, so they never reach the screen or the Z80; a program
that needs them as data (Emacs and WordStar keys, XMODEM) needs another
setting. On the host: `stty -F /dev/ttyUSB0 ixon ixoff`, or
`tools/hostbridge.py --xonxoff ...`.

The [status line](../terminal/status-line.md) shows `RTS/CTS` or
`XON/XOFF` after the line settings.

## Trying it from a PC: hostbridge.py

`tools/hostbridge.py` (in the source) runs a program on the PC with the
terminal as its display: the program's output goes out over the serial
adapter, and your keystrokes on the PC (and on a USB keyboard on the
terminal) plus the terminal's replies come back as its input. Ctrl-]
quits; Ctrl-\ opens SET-UP.

```sh
tools/hostbridge.py /dev/ttyUSB0 vttest   # or: vim, top, $SHELL ...
tools/hostbridge.py --cols 132 /dev/ttyUSB0 $SHELL
tools/hostbridge.py --rtscts /dev/ttyUSB0 $SHELL   # with RTS/CTS flow control
```

It sets `TERM=xterm-color` (8 colors); `--term vt100` turns color off,
`--term xterm-16color` adds bright colors.
