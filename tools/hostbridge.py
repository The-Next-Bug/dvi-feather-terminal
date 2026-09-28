#!/usr/bin/env python3
"""Run a program with the DVI terminal as its display, typing on this PC.

Usage: tools/hostbridge.py [--baud N] [--cols N] [--term NAME] [--rtscts] SERIAL_PORT [COMMAND ...]

COMMAND (default: $SHELL) runs on a pseudo-terminal with TERM=xterm-color
(8 colours, and nothing the terminal lacks; plain vt100 has no colour, so
programs would not use it). Its
output goes to the terminal over SERIAL_PORT; what the terminal sends back
(replies to device attribute and cursor position requests) and what you type
here both become its input. Watch the DVI screen and type in this window.
Ctrl-] quits.

This is how to try vttest, vim, top and friends on the terminal from a PC
(a USB keyboard on the terminal types into the program too). After switching the terminal to 132 columns, tell the program
with `stty cols 132` (or start with --cols 132).

With --rtscts the serial port uses RTS/CTS flow control (wire the adapter's
RTS to the Feather's CTS, GP2/SDA, and its CTS to the Feather's RTS,
GP3/SCL), so bulk output never overruns the terminal.

Only the Python standard library is needed.
"""

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import tty

QUIT = b"\x1d"  # Ctrl-]

BAUDS = {n: getattr(termios, f"B{n}") for n in
         (9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600)
         if hasattr(termios, f"B{n}")}


def open_serial(path, baud, rtscts):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD  # cflag
    if rtscts:
        attrs[2] |= termios.CRTSCTS
    else:
        attrs[2] &= ~termios.CRTSCTS
    attrs[4] = attrs[5] = BAUDS[baud]           # ispeed, ospeed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--baud", type=int, default=115200, choices=sorted(BAUDS))
    ap.add_argument("--cols", type=int, default=80, choices=(80, 132))
    ap.add_argument("--term", default="xterm-color",
                    help="TERM for the program (default: xterm-color; vt100 for no colour)")
    ap.add_argument("--rtscts", action="store_true",
                    help="use RTS/CTS flow control on the serial port")
    ap.add_argument("port")
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    command = args.command or [os.environ.get("SHELL", "/bin/sh")]

    serial = open_serial(args.port, args.baud, args.rtscts)
    pid, master = pty.fork()
    if pid == 0:
        os.environ["TERM"] = args.term
        os.environ.pop("COLORTERM", None)
        os.execvp(command[0], command)

    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 24, args.cols, 0, 0))
    stdin = sys.stdin.fileno()
    saved = termios.tcgetattr(stdin) if os.isatty(stdin) else None
    if saved:
        tty.setraw(stdin)
    print(f"hostbridge: running {' '.join(command)} on {args.port}; Ctrl-] quits\r",
          file=sys.stderr)
    try:
        while True:
            ready, _, _ = select.select([master, serial, stdin], [], [])
            if master in ready:
                try:
                    data = os.read(master, 4096)
                except OSError:  # the program exited
                    break
                if not data:
                    break
                os.write(serial, data)
            if serial in ready:
                os.write(master, os.read(serial, 4096))
            if stdin in ready:
                data = os.read(stdin, 1024)
                if QUIT in data:
                    break
                os.write(master, data)
    finally:
        if saved:
            termios.tcsetattr(stdin, termios.TCSADRAIN, saved)
        try:
            os.kill(pid, signal.SIGHUP)
        except ProcessLookupError:
            pass
        print("\nhostbridge: done", file=sys.stderr)


if __name__ == "__main__":
    main()
