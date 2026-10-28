# Keyboard

## Connecting a USB keyboard

The board is the USB host. Plug a USB keyboard into its USB port: on the
Pico 2 through an OTG adapter or cable, on the Frankless into its hub (with
the USB switch on the hub). A hub works too, or a keyboard with one built
in, but only one: the firmware has room for a single hub (the Frankless's
own hub counts), and with a second hub behind it the USB host may
misbehave: new devices are no longer found, while those already connected
keep working.

On the Pico 2 the port is no longer the board's power input, and the board
does not switch 5 V onto the connector by itself: feed 5 V into **VBUS**
(pin 40), which is the micro-USB connector's VBUS. Never connect a PC or a
charger to the USB port while 5 V is fed in this way: both would drive
VBUS. To flash, disconnect the 5 V supply and the keyboard first, then use
the port as usual (see [Flashing](../start/flashing.md)).

USB sticks can share the port through the hub, for
[moving files](../cpm/moving-files.md) and
[firmware updates](firmware-update.md).

## What the keys send

The keys (US layout) send what a VT100 sends; the full tables are in
[Keys sent](../reference/keys.md). In short:

- Letters honor Caps Lock; Ctrl makes control characters; Alt sends ESC
  first (meta).
- **Return** sends CR (CR LF with New line mode, LNM, on).
- **Backspace** sends BS and Ctrl-Backspace DEL, or the other way round
  (SET-UP's Backspace sends, or the host's DECBKM).
- The **cursor keys** follow the cursor key mode (DECCKM): `ESC [ A` or
  `ESC O A`.
- **F1-F4** are PF1-PF4 (`ESC O P` to `ESC O S`).
- The **keypad** follows the keypad mode (DECKPAM/DECKPNM) while Num Lock
  is on, and is cursor and editing keys when it is off, as on a PC. The
  PC's keypad + is the VT100's keypad comma.
- **Home, End, Insert, Delete, Page Up/Down and F5-F11** send the
  xterm/VT220 codes (`ESC [ H`, `ESC [ 2 ~`, ...).
- Keys repeat after 0.5 s at about 30 a second. Caps Lock and Num Lock
  light the keyboard's LEDs.

Keys with a special meaning:

| Key | Action |
|---|---|
| **F12**, **Pause** | open [SET-UP](setup.md) (nothing is sent) |
| **F11** | in CP/M mode: reset the Z80 machine; in terminal mode it sends `ESC [ 2 3 ~` |
| any key while the screen is blanked | wakes it (not sent) |

In VT52 mode and on the ADM-3A the keys send what those terminals send
(see [Terminal types](terminal-types.md)). SET-UP and the CP/M monitor
always get the VT100's keys.

## Checking a keyboard

The [status line](status-line.md) shows `kbd` while a keyboard is
attached and the last key sent (`key ESC [A`, `key ^C`, ...), so a
keyboard can be checked without a host.
