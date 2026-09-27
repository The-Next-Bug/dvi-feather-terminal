#ifndef VIDEO_H
#define VIDEO_H

// DVI output at 800x600p60. The screen is rasterised on core 0
// into per-line pixel buffers (video_update); core 1 encodes those to TMDS
// one scanline at a time and draws the cursor.
//
// This is the libdvi (PIO) backend. The rasteriser (raster.c) is portable,
// so an RP2350 HSTX backend can implement the same calls.

#include <stdint.h>

#include "screen.h"

// Raise the core voltage, set the system clock to the TMDS bit clock and set
// up libdvi. Call before any peripheral that depends on clk_sys/clk_peri
// (e.g. the UART) is initialised.
void video_init(void);

// Rasterise every dirty line of the screen and clear its dirty flags. Call
// from core 0 after changing the screen; changes are not shown until then.
void video_update(screen_t *screen);

// Diagnostics, collected when built with VIDEO_DIAG (zero otherwise).
typedef struct {
	uint32_t frames;           // frames rendered by core 1
	uint32_t max_line_cycles;  // longest scanline encode since the last call
	uint32_t late;             // libdvi's count of scanlines not ready in time
	uint32_t cycles_budget;    // system clock cycles per scanline
	uint32_t core1_stage;      // 1 started, 2 starting DVI, 3 DVI started, 4 in frame loop
} video_stats_t;

// Read the diagnostics and reset max_line_cycles.
video_stats_t video_stats(void);

// Rasterise the whole screen and launch core 1. The screen must outlive the
// program.
void video_start(screen_t *screen);

#endif
