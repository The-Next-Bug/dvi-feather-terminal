# Status line

Row 24, below the 24 text rows, shows the terminal's state.

## In terminal mode

```
 VT100 80x24 | v0.11.0 | 115200 8N1 RTS/CTS | kbd | USB | echo | key ESC [A     14:05
```

- the terminal type (`VT100`, `VT52`, `ADM-3A`) and the width,
- the firmware build,
- the baud rate and framing, then `RTS/CTS` or `XON/XOFF` with
  [flow control](../start/host.md#flow-control),
- `kbd` while a keyboard is attached,
- the USB stick, as in CP/M mode (below),
- `echo` when local echo is on,
- the last key sent to the host (`key ESC [A`, `key ^C`, `key DEL`, ...),
- at the right, the [clock](clock.md) once its time is known. When the
  clock needs the room, the end of the line gives way to it.

## In CP/M mode

```
 CP/M A B - - E - - - | USB | 18.42 MHz | kbd | v0.11.0                         14:05
```

- `CP/M` and the drives A-H as letters (`-` for an empty one). A letter
  lights up in reverse video while that drive is read or written, like a
  drive's busy light.
- `USB` while a USB stick is in use as `USB:` (lit while it is read or
  written); `+1` when another stick waits to take over, `?` when one could
  not be used, `usb` when none is usable.
- the emulated clock rate in MHz, or `monitor` while the monitor runs,
- `kbd` and the build, and the clock.

## Debug lines

SET-UP's **Status line** can show diagnostics instead, refreshed four
times a second.

**debug**: `late T+N|line C/P%|Ffps|loop M/Qms|render R/Sus|Z MHz|snd A%`

| Field | Meaning |
|---|---|
| `late T+N` | scanlines not ready in time for the DVI output (all, + since the last refresh); should stay 0 |
| `line C/P%` | the video core's longest scanline as a share of the time a scanline lasts (now / peak since debug came on) |
| `Ffps` | frames a second |
| `loop M/Qms` | the main loop's longest pass (now / peak) |
| `render R/Sus` | the longest single line render (now / peak) |
| `Z MHz` | CP/M mode: the Z80's speed |
| `snd A%` | boards with a DAC: the share of the CPU the sound takes |

**debug usb**: `1234s | usb: 1s led1 1s msc2 1s up2 10s vol2`: the
seconds since power-on (if they stop, the main loop has stopped), then the
USB host's last four events, oldest first, each as its time, the event and
the device's USB address (`xN` for repeats):

| Event | Meaning |
|---|---|
| `up`, `down` | a device set up, or gone |
| `kbd`, `kbd-` | a keyboard attached, detached |
| `msc`, `msc-` | a USB stick attached, detached |
| `vol`, `vol?`, `unvol` | the stick mounted as `USB:`, not mountable, unmounted |
| `timeout`, `gone`, `ioerr`, `busy` | a stick transfer over 3 s, its stick pulled, failed, refused |
| `led`, `led?` | the keyboard LEDs sent, or refused (busy) |
| `stuck`, `probe?`, `probe!`, `nprobe` | the USB host restarted because it looked stuck |
| `mscrst` | restarted because a USB stick stopped answering (`timeout` or `badcsw` before it) |
| `rstwait` | a restart waiting because the last one was too recent |

See [Troubleshooting](troubleshooting.md) for what the firmware does about
a stuck USB host.

## Hiding it

**Status line** `hidden` leaves row 24 blank. The text area is still 24
rows; the logo's build line and CP/M's drive lights are not shown.
