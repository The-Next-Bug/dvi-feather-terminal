# SET-UP

SET-UP is a panel over the bottom of the screen for the settings kept in
flash. The selected setting is marked `>` and its name is bold; each
setting's current choice is shown in reverse video.

![SET-UP](../images/generated/setup.png)

## Opening it

- **F12** (or **Pause**) on the USB keyboard. While it is open, data from
  the host waits in the receive buffer (with flow control the host is held
  off).
- **`CSI ? 999 ~`** (`ESC [ ? 9 9 9 ~`) from the host, a sequence private
  to this terminal. The bytes the host sends next are taken as keys, so it
  can be driven from a PC: `tools/hostbridge.py` sends it on Ctrl-\ and
  passes your keys to the terminal until Return or Esc. (Not on the
  ADM-3A, which has no such sequence; F12 still works.)

## Keys

| Key | Action |
|---|---|
| Up, Down | select a setting |
| Left, Right, Space | change it |
| **Return** | save to flash and apply (a new width clears the screen, as the host's DECCOLM does) |
| **Esc**, F12 | leave without saving |
| **T** | show the [demo screen](../start/displays.md) in the edited colors until a key |
| **R** | restart the board, as at power-on (unsaved changes are dropped; CP/M drives are written back first): the last resort when the terminal is stuck |
| **U** | [update the firmware](firmware-update.md) from `FIRMWARE.UF2` on the SD card or a USB stick |
| **C** | the [Clock and sound panel](#the-clock-and-sound-panel) |
| **P** | the Printer panel: the Z80 machine's printer through a WiFi modem; see [Printing](../cpm/printing.md#the-printer-panel) |
| **N** | terminal mode: the Telnet panel, for a host reached through a WiFi modem; see [Telnet](telnet.md#the-telnet-panel) |
| **D** | CP/M mode: the [Drives panel](../cpm/drives.md#the-drives-panel) |
| **M** | CP/M mode: back to the [monitor](../cpm/monitor.md) |

## The settings

{{#include ../generated/settings-main.md}}

| Setting | Notes |
|---|---|
| Columns | the width at power-on, and after F11 in CP/M mode. With Terminal type ADM-3A it shows `80 (ADM-3A)` and is skipped: the ADM-3A has 80 columns only |
| Backspace sends | what Backspace sends at power-on and after a reset (DECBKM); BS suits vttest and CP/M software |
| Local echo | keys typed are shown on the terminal as well as sent |
| Port | boards with a WiFi modem only (the Frankless): the serial port as a plain serial line, or the WiFi modem wired to it. Saving a new Port restarts the board. With the WiFi modem, Baud rate, Data/parity/stop and Flow control show its fixed line (`921600`, `8N1`, `RTS/CTS`, each marked `(WiFi modem)`) and are skipped, and Return on the Port row opens the modem's settings |
| Baud rate, Data/parity/stop | the serial line; applied as soon as SET-UP saves |
| Flow control | see [Connecting a host](../start/host.md#flow-control); RTS/CTS only on boards with the pins |
| New line (LNM) | at power-on and after a reset: Return sends CR LF, and LF also returns the cursor. Not used in CP/M mode |
| Colors, Palette | see [Colors and palettes](colors.md). Two preview rows under Palette show the default text, bold, the status line and the 16 ANSI colors as text and as backgrounds |
| Display | 1024x600 or 800x600; saving a new one restarts the board and asks to keep it (see [Displays](../start/displays.md)) |
| Mode | a serial terminal, or the console of the [Z80 CP/M machine](../cpm/overview.md); saving a new mode restarts the board |
| Status line | shown, hidden (row 24 stays blank), or the [debug lines](status-line.md#debug-lines) |
| Blank after | the [screen blanking](../start/displays.md#screen-blanking) time |
| Terminal type | VT100, VT52 or ADM-3A at power-on and after a reset; see [Terminal types](terminal-types.md). Changing to or from ADM-3A clears the screen |
| Character set | UTF-8 or Latin-1: what the host's bytes from 80h mean, for the VT100 in terminal mode; see [Character set](../reference/charset.md#utf-8). With the VT52, the ADM-3A or in CP/M mode it shows `Latin-1` with the reason and is skipped |
| USB log | keep a log of the USB host in `USBLOG.TXT` on the SD card; see [Troubleshooting](troubleshooting.md#the-usb-log) |

The panel opens with the width, Backspace mode and LNM currently in use,
so Return alone makes a switch the host made (DECCOLM, DECBKM, LNM) the
power-on default. While SET-UP is open the whole screen shows the edited
Colors and Palette; leaving without saving puts the old ones back.

**T** does not keep the terminal's screen (there is no memory for a copy),
so SET-UP comes back over a cleared screen, as after a width change.

## The Clock and sound panel

**C** opens a second panel for the clock and the sound:

![The Clock and sound panel](../images/generated/clockpanel.png)

{{#include ../generated/settings-panel.md}}

- **Date** and **Time** show the clock as it runs; typing digits over them
  (Left/Right move the cursor, Backspace goes back) and Return sets the
  clock, and the real-time clock chip with it. See [Clock](clock.md).
- **Status line clock**, **Seconds** and **Date** choose what the status
  line shows; the row under them shows the result.
- **Bell**, **Sound chip**, **Stereo** and **Sound DAC**: see
  [Bell and sound](sound.md). A new Bell level beeps, a new Sound chip
  level or Stereo layout plays a chime.
- Return takes the choices back to SET-UP, whose Return saves them with the
  other settings; Esc goes back without them.

## The Modem panel

With Port on the WiFi modem (boards wired for one: the Frankless),
**Return** on the Port row opens the Modem panel:

- **Network** and **Password**: the WiFi network the modem joins, kept in
  the terminal's flash (the password in plain text) and given to the
  modem at every start, so a reflashed or replaced module joins by itself.
  Type them (Backspace takes the last character off; the password shows as
  stars); **Return** on either saves them and has the modem join. Left
  empty, the modem keeps the network it was given some other way.
  Zimodem cannot take a comma or a quote in the name, or a quote in the
  password.
- **Address**: DHCP.
- **Reset modem**: resets the module and sets it up again, as at power-on.
- **Defaults** (Return twice): the module's factory settings, then the
  terminal's own (rate, flow control, the DCD and DTR lines, the network).
- **Update firmware**: Return finds `ZIMODEM.ZMI` on the SD card or a USB
  stick and checks it; Return again writes it into the module; see
  [Updating the module's firmware](wifi-modem.md#updating-the-modules-firmware).
- **Status**, **Firmware**, **Problem**: where the modem's start-up is,
  the network it is on and its address, Zimodem's version, the ESP8266's
  last reset cause, and the last thing that went wrong.

Every start in WiFi modem mode (power-on, R, a crash) resets the module
and sets it up; the status line says `WiFi modem: ...` meanwhile, and the
serial port is the modem's until it is done. The wiring, the start-up's
steps and what goes wrong: [WiFi modem](wifi-modem.md).

## Settings in flash

The settings live in two flash sectors used in turn, each save with a
sequence number and a checksum, so a save cut short by a power loss keeps
the previous settings. Flashing new firmware keeps them. A save holds up
the terminal for 30-50 ms (the flash erase); the picture stays.
