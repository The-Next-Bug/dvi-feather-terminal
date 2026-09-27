#ifndef VIDEO_H
#define VIDEO_H

// DVI output. Renders a screen_t as 80x30 cells of 8x16 at 640x480p60, one
// scanline at a time on core 1 (no framebuffer).
//
// This is the libdvi (PIO) backend. Everything the renderer needs is in
// screen_t and font.h, so an RP2350 HSTX backend can implement the same two
// calls.

#include "screen.h"

// Raise the core voltage, set the system clock to the TMDS bit clock and set
// up libdvi. Call before any peripheral that depends on clk_sys/clk_peri
// (e.g. stdio UART) is initialised.
void video_init(void);

// Launch core 1 rendering from the given screen. The screen must outlive the
// program.
void video_start(const screen_t *screen);

#endif
