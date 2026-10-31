# Troubleshooting

## No picture

- **At power-on**: monitors take a few seconds to show a new signal; the
  logo shows for 10 seconds. Check the DVI/HDMI cable and that the monitor
  takes 1024x600 at 60 Hz; if not, use 800x600 (below).
- **After changing the Display mode**: wait 15 seconds, or switch the board
  off and on; it goes back to the old mode (see
  [Displays](../start/displays.md#changing-the-display-mode)).
- **The monitor shows neither mode**: flash a firmware built with
  `-DDVI_MODE=800x600` (see [Building from source](../hacking/building.md));
  its default is 800x600 until a mode is saved. A saved Display setting
  still wins over it.
- **After the screen blanked**: press a key; the monitor takes a few
  seconds to find the signal, and the firmware gives it a second start
  after 3 seconds.

## The keyboard does nothing

- Check `kbd` on the [status line](status-line.md): no `kbd` means the USB
  host does not see a keyboard. On the Pico 2 and the Feathers the keyboard
  needs 5 V, from a powered OTG adapter or fed into VBUS (a Feather's USB
  pin; see [Keyboard](keyboard.md)); on the Frankless the USB switch must be
  on the hub.
- Up to three hubs are supported (the Frankless's own counts). A fourth
  shows on the monitor's `USB:` line as `0 (hub n port p) setting up`
  for about 20 s, while devices plugged in meanwhile wait; then its port
  is turned off and the line says `1 refused (too many hubs or devices)`.
  Unplugging it and plugging it in again tries it again.
- The screen is blanked: the first key only wakes it.
- `key ...` on the status line changes but the host sees nothing: check the
  serial wiring and line settings ([Connecting a host](../start/host.md)),
  and Flow control (`RTS/CTS` without the wires holds every key back).

## USB sticks

The stick must be FAT12, FAT16 or FAT32 with 512-byte sectors (most up to
32 GB; larger ones are usually exFAT and need formatting as FAT32 first).
The status line shows `USB` when a stick is in use, `?` when one could not
be used. Pull a stick only when `USB` is not lit.

## The USB host restarts by itself

The firmware checks the USB host. When a device has been setting up for
over 20 seconds, the hub (or the device on the port) stops answering a
status request sent every 5 seconds, or a USB stick stops answering in the
middle of a command, it restarts the USB host: every device is dropped and
set up again, without resetting the terminal or the Z80 machine; the
keyboard is gone for about half a second, and a stick in use as `USB:` is
unmounted and mounted again. If restarting does not help, it waits longer
each time before the next (up to 16 times as long). The `debug usb`
status line shows the events.

## The USB log

With SET-UP's **USB log** set to `SD card USBLOG.TXT` (and saved), the USB
host keeps a log in `USBLOG.TXT` in the SD card's root, created if it is
not there. Every few seconds the USB events, TinyUSB's own error messages
and any restart (with the devices it saw) are added, each line with the
seconds since power-on; each power-on starts with a line giving the
firmware and the time. Set it back to `off` to stop (the file stays). In
terminal mode the card is mounted for the log; in CP/M mode it is written
only while the monitor has the card mounted.

## Crash reports

If the firmware stops (a crash, or the main loop stuck for 8 seconds: no
clock, no keyboard), the status line says so: `PANIC ...`,
`HARD FAULT ...` or `STUCK ...`, with the code addresses involved, which
identify the place with the build's `dvi_terminal.elf` (CI keeps it with
each build). After a crash, or 20 seconds stuck inside an interrupt
handler, the board restarts by itself after 20 seconds; the next start
shows `The last run ended with: ...` under the banner and adds it to the
USB log. Please include that line and the firmware version in a bug report.

`STUCK` also shows during long operations that keep the main loop busy,
such as formatting a card or checking a firmware file; it goes away once
they finish.

## The status LED

Boards with a status LED (see [Boards](../start/boards.md)): off while the
screen is on, flashing while the SD card, a USB stick or the serial line is
busy. With the screen blanked it also flashes once a second, so a board
that has hung shows a light that stays on or stays off.

## Starting over

- **R** in SET-UP restarts the board, as at power-on.
- To flash afresh, use BOOTSEL ([Flashing](../start/flashing.md)); the
  settings are kept.
