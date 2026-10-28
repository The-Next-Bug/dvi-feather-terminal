# Flashing

## The release ZIP

Each release (`dvi-terminal-<version>.zip`) holds:

| File | What it is |
|---|---|
| `dvi_terminal-<target>.uf2` | the firmware for one board (see [Boards](boards.md)) |
| `dvi_terminal-<target>.bin` | the same as a raw binary, for other flashing tools |
| `sdcard-<target>/FIRMWARE.UF2` | the same UF2 under the name the [firmware update](../terminal/firmware-update.md) looks for |
| `build-info-<target>.txt` | how it was built: versions, options |
| `guide/` | this guide (`guide/index.html`) |
| `FLASHING.txt` | these steps in short |
| `CHANGES.md` | what changed since the previous release |
| `LICENSE`, `THIRD-PARTY.md`, `licenses/` | the licenses |
| `SHA256SUMS` | checksums of the files |
| the source | the source of the built commit |

## Flashing over USB (BOOTSEL)

1. Unplug the board. On boards whose USB port is the keyboard's host
   port, disconnect any 5 V supply fed into VBUS and the keyboard first
   (see [Keyboard](../terminal/keyboard.md)).
2. Hold the **BOOTSEL** button and plug the board into a PC by USB (on the
   Frankless, set the USB switch to the USB-C socket). A drive named
   `RP2350` appears.
3. Copy `dvi_terminal-<target>.uf2` for your board onto that drive. The
   board restarts with the new firmware.

This always works, whatever state the firmware is in, so it is also the
way back after a failed [firmware update](../terminal/firmware-update.md).

Once the firmware runs, later versions can be installed from an SD card or
a USB stick without a PC: see [Firmware updates](../terminal/firmware-update.md).

## First start

At power-on the startup logo shows for 10 seconds, with the build on the
status line (monitors take a few seconds to show a new signal); a key or
the first byte from the host ends it early. Then the terminal starts with
the default settings: 1024x600, 80 columns, VT100, 115200 8N1 on the
serial port, no flow control.

The settings are kept in flash across power-offs and firmware updates.
