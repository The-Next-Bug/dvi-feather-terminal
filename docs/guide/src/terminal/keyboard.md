# Keyboard

## Connecting a USB keyboard

The board is the USB host. Plug a USB keyboard into its USB port: on the
Pico 2 and the Feathers through an OTG adapter or cable, on the Frankless
into its hub (with the USB switch on the hub). Hubs work too, and
keyboards with one built in: up to three hubs in all (the Frankless's own
counts), so on the Frankless one more hub with a hub keyboard behind it.
Two hubs have been tried; a third is experimental (not yet tried on
hardware).
A fourth hub is refused: after about 20 s its port is turned off, and
devices plugged in meanwhile are set up then.

On the Pico 2 and the Feathers the port is no longer the board's power
input, and the board does not switch 5 V onto the connector by itself, so
the keyboard needs 5 V from elsewhere. Either:

- **A powered USB OTG adapter** (an OTG adapter or hub with its own power
  input): its supply powers the keyboard, and the board too, through its
  USB port. Nothing to wire.
- **5 V fed into the board**: on the Pico 2 into **VBUS** (pin 40), on a
  Feather into its **USB** pin; both are the USB connector's 5 V, so the
  keyboard gets it through a plain OTG adapter.

Never connect a PC or a charger to the USB port while 5 V is fed in either
way: both would drive VBUS. To flash, disconnect the 5 V supply (or the
powered adapter) and the keyboard first, then use the port as usual (see
[Flashing](../start/flashing.md)).

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
