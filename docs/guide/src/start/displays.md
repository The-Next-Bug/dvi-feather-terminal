# Displays

## The two modes

| Display | Timing | Pixel clock | Text |
|---|---|---|---|
| **1024x600** (the default) | 60 Hz, reduced blanking (CVT-RB, 1184 x 619) | 44 MHz | 80 columns of Terminus 12x24 (960 pixels wide, centered); 132 columns of misc-fixed 6x12 in 7-pixel cells, each glyph row on two scanlines (924 pixels, centered) |
| **800x600** | 60 Hz (VESA's, with a 44-pixel front porch) | 40 MHz | 80 columns of Terminus 10x20 across the full width; 132 columns of 6x12 in 6-pixel cells (792 pixels, centered) |

Both have 25 rows of 24 scanlines: 24 text rows and the status line.
1024x600 is the native size of the common 7" HDMI panels, so every pixel
is shown as it is, unscaled; desktop monitors take it as well. 800x600
suits monitors that do not take 1024x600.

![The demo screen at 1024x600, 80 columns](../images/generated/demo-1024-80.png)

![The demo screen at 1024x600, 132 columns](../images/generated/demo-1024-132.png)

![The demo screen at 800x600, 80 columns](../images/generated/demo-800-80.png)

## Changing the display mode

SET-UP's **Display** row picks the mode. Saving a new one restarts the
board in it, and the first screen asks whether to keep it:

![The prompt after changing the display mode](../images/generated/display-prompt.png)

- Any key keeps the new mode (the key goes nowhere else).
- Without a key within 15 seconds (say the monitor shows nothing in the new
  mode), the board goes back to the old mode and restarts again.
- Until a key keeps it, every start asks, so switching the board off and
  on during the wait also comes back to the old mode.

A release build starts in 1024x600 until a mode is saved; a build made
with `-DDVI_MODE=800x600` starts in 800x600 (see
[Building from source](../hacking/building.md)).

## Screen blanking

SET-UP's **Blank after** (never, 1, 5, 10, 30 or 60 minutes) turns the
screen off after that long without a key. The DVI signal stops and the
outputs are left undriven, as if the cable were pulled, so the monitor goes
into power saving and turns its backlight off. Output from the host or the
Z80 does not wake it; it carries on unseen. The next key wakes it and is
not sent anywhere.

On waking the monitor has to find the signal again, which takes it a few
seconds. Three seconds after waking the signal stops for half a second
once more: a monitor that woke without locking to it gets a fresh start
(one that had locked locks again).

## The startup logo

At power-on the logo shows for 10 seconds, tinted with the Colors setting,
with the build on the status line. A key or the first byte from the host
ends it early. It is not shown on the boot that asks to keep a new display
mode.

## Test pattern

`hstx_test.uf2` (built with the firmware, not in the release ZIP) shows a
test pattern instead of the terminal: a border on the outermost pixels,
the 64 basic colors, a 1-pixel checkerboard with a moving line, and level
ramps; the serial port prints the clocks and the frame rate every second.
It shows the build's default mode.
