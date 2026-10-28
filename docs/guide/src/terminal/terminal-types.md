# Terminal types

SET-UP's **Terminal type** picks what the terminal is at power-on and after
a reset, in either mode, so CP/M software set up for one of these works on
the built-in Z80 too. The status line names the type in use.

## VT100

A DEC VT100 with the VT102's editing functions, plus what xterm-based
software expects: 16, 256 and 24-bit colors, the alternate screen,
repeat (REP), and the editing keys. Checked with `vttest` (menus 1-3, 6 and
8). The sequences are listed in [VT100 escape sequences](../reference/vt100.md).

Limitations:

- The alternate screen starts blank each time (xterm keeps what was last on
  it), and `?47` works as `?1047`. SET-UP opened over it at 132 columns has
  no room to save the rows under its panel: they come back blank (most
  programs redraw them on Ctrl-L).
- Double-width and double-height lines show as normal lines; scrolling is
  jump scrolling only.
- OSC, DCS, SOS, PM and APC strings (window titles, sixels, ...) are
  discarded. A string that is never ended (an Alt+] typed by accident sends
  `ESC ]`) ends at the next control character, which then acts as usual,
  or after 1024 bytes.

## VT52

The VT100's own VT52 mode, which the host can also switch to with
`CSI ? 2 l` (and back with `ESC <`): cursor moves, direct addressing
(`ESC Y`), erasing, reverse line feed, line graphics, and keypad modes.
The cursor keys, PF keys and keypad send what a VT52 sends; keys a VT52
does not have (Home, End, Insert, Delete, Page Up/Down, F5-F11) send
nothing. See [VT52 and ADM-3A](../reference/vt52-adm3a.md).

## ADM-3A

A Lear Siegler ADM-3A as terminfo's `adm3a` describes it, plus the
Kaypro's additions (terminfo `kaypro`): control characters move the cursor
and clear the screen, `ESC =` addresses it, and printing wraps at once in
the last column. No colors or attributes, no replies, 80 columns only; the
8th bit of each byte is ignored. The cursor keys send ^K ^J ^L ^H and Home
^^; other editing and function keys send nothing. The host cannot open
SET-UP in this mode; F12 still does. See [VT52 and ADM-3A](../reference/vt52-adm3a.md).

## Checking them

[TERMTEST](../cpm/tools/termtest.md) on the `Z80TESTS` disk shows each
type's functions page by page, with what each page should look like.
