#ifndef PALETTE_H
#define PALETTE_H

// The color palette. Portable C (no pico-sdk), host-tested.
//
// Screen cells hold xterm-256 palette indices, not colors: 0-15 the ANSI
// colors, 16-231 the 6x6x6 color cube, 232-255 the gray ramp. What an
// index looks like is only decided when core 1 composes a scanline, through
// a table palette_build makes from the Colors and Palette settings, so a
// new palette needs no change to the screen. The terminal's default colors
// are ANSI entries too: text 7, bold text 15, background 0.
//
// Tables, by hue (the Colors setting) and mono (the Palette setting):
//   - white, color: our ANSI set (the old RGB222 levels) and xterm's 16-255.
//   - green/amber/blue, color: 0-15 a ramp in the phosphor's hue, xterm's
//     16-255 as they are.
//   - mono: every entry a ramp in the hue (white: gray) by brightness.
// The ANSI ramp goes by bands, so that normal text (7) keeps its old level
// and no color fades out (blue would, by brightness alone): black; dark
// gray (8); the normal colors, dimmest first; 7; the bright colors; 15.

#include <stdbool.h>
#include <stdint.h>

#define PALETTE_SIZE 256

// Indices the terminal and the status line use.
#define PALETTE_BLACK 0
#define PALETTE_BLUE 4
#define PALETTE_TEXT 7        // default text (SGR 39)
#define PALETTE_DIM 8         // bright black: dark gray
#define PALETTE_BOLD_TEXT 15  // bold default text
#define PALETTE_BACKGROUND 0  // default background (SGR 49)

// Hues: the Colors setting's values.
enum { PALETTE_HUE_WHITE, PALETTE_HUE_GREEN, PALETTE_HUE_AMBER, PALETTE_HUE_BLUE, PALETTE_HUES };

// The table for hue (out of range: white) and mono, as RGB565 pixels.
void palette_build(uint16_t out[PALETTE_SIZE], unsigned hue, bool mono);

// The xterm entry (16-255: cube or gray ramp) nearest to a 24-bit color.
// The ANSI entries are left out: their colors depend on the palette.
uint8_t palette_nearest(uint8_t r, uint8_t g, uint8_t b);

// xterm's standard color for index i (16-255), as 8-bit channels.
void palette_xterm_rgb(unsigned i, uint8_t rgb[3]);

#endif
