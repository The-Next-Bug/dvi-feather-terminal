#ifndef VIDEO_H
#define VIDEO_H

// DVI output at 800x600 through the RP2350 HSTX (hstx_dvi.c). Changed
// screen lines are rasterized on core 0 (video_update); core 1 composes
// each scanline from them into RGB565 (raster_compose), shows the cursor and
// the startup logo, and the HSTX's TMDS encoder does the rest.

#include <stdbool.h>
#include <stdint.h>

#include "palette.h"
#include "screen.h"
#include "splash.h"

// Raise the core voltage, set the system clock (10 x the pixel clock) and
// set up the HSTX. Call before any peripheral that depends on
// clk_sys/clk_peri (e.g. the UART) is initialized.
void video_init(void);

// Show the screen's palette indices in these colors (RGB565; see
// palette_build) from the next frame, without re-rendering anything. Call
// before video_start too (until then nothing is shown in them). From core
// 0's main loop: it may wait up to a frame for core 1 to take the previous
// table.
void video_set_palette(const uint16_t colors[PALETTE_SIZE]);

// Turn the DVI signal off (blank.h), or on again. Off, there is no signal
// at all, so the monitor can power down (backlight included); back on, it
// has to resync, which takes it a few seconds. The screen goes on being
// updated underneath, and core 1 keeps frame time, so video_set_palette and
// video_splash do not wait for good.
void video_set_blank(bool blank);

// Rasterize every dirty line of the screen and clear its dirty flags. Call
// from core 0 after changing the screen; changes are not shown until then.
void video_update(screen_t *screen);

// Diagnostics, collected in every build (the debug status line; VIDEO_DIAG
// prints them on UART0 too).
typedef struct {
	uint32_t frames;           // frames composed by core 1
	uint32_t max_line_cycles;  // longest scanline compose since the last call
	uint32_t late;             // scanlines not composed in time for the DMA
	uint32_t cycles_budget;    // system clock cycles per scanline
	uint32_t core1_stage;      // 1 started, 2 starting HSTX, 3 HSTX started, 4 in frame loop
	uint32_t max_render_us;    // longest raster_render_line (core 0) since the last call
} video_stats_t;

// Read the diagnostics and reset max_line_cycles and max_render_us.
video_stats_t video_stats(void);

// Rasterize the whole screen and launch core 1. The screen must outlive the
// program.
void video_start(screen_t *screen);

// Show img (see splash.h) in place of the text rows until video_splash_end;
// the status line is still the screen's. For startup, after video_start and
// before the text rows scroll (a copy of the image data goes in the text
// rows' raster memory, as core 1 must not read flash; the status line must
// still be in its own, last, slot). Returns false, showing the screen as
// before, if img cannot be shown. tint (splash_tint) gives its colors.
bool video_splash(const splash_image_t *img, const splash_tint_t *tint);

// Back to the text rows (all re-rendered).
void video_splash_end(void);

#endif
