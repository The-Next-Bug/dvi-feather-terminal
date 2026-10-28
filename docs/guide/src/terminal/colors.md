# Colors and palettes

Two SET-UP settings decide how colors look; both apply to everything on
the screen at once, text already there included (the screen holds palette
entries, and only the table that turns them into pixels changes).

## Colors

- **white**: the 16 ANSI colors as on a VGA screen.
- **green**, **amber**, **blue**: phosphor shades. The 16 ANSI colors
  (default text and bold included) become shades of that hue by
  brightness: black; dark gray; the normal colors; default text; the bright
  colors; bold. 256-color and 24-bit colors stay in color.

The status line, SET-UP's box and the startup logo match (white keeps the
logo's own colors).

## Palette

- **color**: as above.
- **mono**: every color, 256-color and 24-bit too, becomes a shade of the
  Colors hue (with white: gray) by its brightness, so colors of similar
  brightness look alike. The status line becomes the text colors reversed,
  and with white the startup logo shows in gray.

| | |
|---|---|
| white, color | ![White](../images/generated/terminal.png) |
| green, mono | ![Green phosphor](../images/generated/terminal-green.png) |
| amber, mono | ![Amber phosphor](../images/generated/terminal-amber.png) |

## Colors from the host

- 8 and 16 colors: SGR 30-37, 40-47, 90-97, 100-107; bold also brightens
  colors 0-7 to 8-15.
- 256 colors: `SGR 38;5;n` and `48;5;n`, the xterm palette, exactly.
- 24-bit: `SGR 38;2;r;g;b` and `48;2;r;g;b`, shown as the nearest of the
  256.

See [VT100 escape sequences](../reference/vt100.md#sgr-attributes-and-colors).

While SET-UP is open, the screen shows the edited Colors and Palette, and
its two preview rows show the 16 ANSI colors; leaving without saving puts
the old ones back.
