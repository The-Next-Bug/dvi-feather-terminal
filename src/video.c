#include "video.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"

#include "diag.h"
#include "hstx_dvi.h"
#include "palette.h"
#include "raster.h"
#include "splash.h"

_Static_assert(SCREEN_ROWS * RASTER_ROW_HEIGHT == RASTER_ACTIVE_HEIGHT, "rows fill the screen");

// Cursor blink: on for 32 frames, off for 32 (about 1 Hz at 60 Hz).
#define CURSOR_BLINK_FRAMES 32
// Blinking text (SGR 5): shown for this long, then hidden for as long.
#define BLINK_HALF_PERIOD_MS 500

// Core 1 composes scanlines into a ring of LINE_BUFS buffers, at most
// AHEAD lines beyond the one the DMA interrupt last handed out: a buffer
// stays in use until the interrupt asks for the line two after it (see
// hstx_line_fn), so line k may overwrite line k - LINE_BUFS once the
// interrupt has asked for line k - 2.
#define LINE_BUFS 4
#define AHEAD (LINE_BUFS - 2)

// Everything core 1 (and its interrupt) reads lives in SRAM (not const,
// which would put it in flash): an XIP stall would starve the HSTX.
static uint32_t line_buf[LINE_BUFS][RASTER_LINE_WORDS];
// Scanlines are numbered from the first one of the first frame, so frame
// and line are seq / RASTER_ACTIVE_HEIGHT and seq % RASTER_ACTIVE_HEIGHT.
// Unsigned and compared by difference, so they may wrap.
static volatile uint32_t requested = UINT32_MAX; // last line handed to the DMA
static volatile uint32_t composed;               // lines composed so far
static volatile uint32_t late_lines;

// The palette (video_set_palette): index c as an RGB565 pixel in both
// halves of a word. Two tables: core 0 fills the one core 1 is not using,
// and core 1 switches at the start of a frame, so no frame mixes the two.
static uint32_t palette_words[2][PALETTE_SIZE];
static volatile uint8_t palette_next; // core 0: the table for the next frame
static volatile uint8_t palette_used; // core 1: the table of the current frame
static bool core1_launched;

// diag_frames counts frames in every build: video_splash waits on it.
static volatile uint32_t diag_frames, diag_max_cycles;
// Core 1 progress: 1 started, 2 starting HSTX, 3 HSTX started, 4 in frame loop.
static volatile uint32_t diag_core1_stage;
static screen_t *video_screen;

// One raster line per screen line slot (indexed like screen_t.lines).
static raster_line_t raster[SCREEN_ROWS];

// The splash (video_splash) keeps a copy of its image data in the raster
// memory of the text rows' slots, which are not rendered meanwhile. At
// startup the status line is in the last slot, clear of it.
#define SPLASH_SLOTS (SCREEN_ROWS - 1)
static uint8_t *const splash_data = (uint8_t *)(void *)raster;
#define SPLASH_DATA_MAX (SPLASH_SLOTS * sizeof(raster_line_t))
// Core 1 shows the splash for the text rows unless SPLASH_OFF: black while
// SPLASH_BLACK (the data is being written), the image while SPLASH_SHOW,
// decoded from splash_start with splash_colours per level.
enum { SPLASH_OFF, SPLASH_BLACK, SPLASH_SHOW };
static volatile uint8_t splash_state;
static splash_decoder_t splash_start;
static uint16_t splash_colours[4];
// Core 0: the splash owns those slots (video_update leaves them alone).
static bool splash_holds_raster;

// What core 1 latched at the start of the frame it is composing: the mode
// and cursor cannot tear between rows.
static struct {
	const screen_t *s;
	uint8_t splash;
	uint cols, cursor_row;
	int cursor_col; // -1: not shown this frame
	splash_decoder_t decoder;
	uint16_t colours[4];
	const uint32_t *palette;
} frame;

static void __not_in_flash_func(start_frame)(uint32_t n) {
	const screen_t *s = video_screen;
	frame.s = s;
	frame.cols = s->cols;
	bool cursor_on = s->cursor_visible && !(n / CURSOR_BLINK_FRAMES & 1);
	frame.cursor_row = s->cursor_row;
	frame.cursor_col = cursor_on ? (int)s->cursor_col : -1;
	frame.splash = splash_state;
	uint8_t p = palette_next;
	palette_used = p;
	frame.palette = palette_words[p];
	if (frame.splash == SPLASH_SHOW) {
		frame.decoder = splash_start;
		for (int i = 0; i < 4; ++i)
			frame.colours[i] = splash_colours[i];
	}
}

static void __not_in_flash_func(compose)(uint32_t seq) {
	uint y = seq % RASTER_ACTIVE_HEIGHT;
	if (y == 0)
		start_frame(seq / RASTER_ACTIVE_HEIGHT);
	uint32_t *px = line_buf[seq % LINE_BUFS];
	if (frame.splash != SPLASH_OFF && y < SPLASH_HEIGHT) {
		if (frame.splash == SPLASH_SHOW)
			splash_compose((uint16_t *)px, &frame.decoder, y, frame.colours);
		else
			for (uint i = 0; i < RASTER_LINE_WORDS; ++i)
				px[i] = 0;
		return;
	}
	uint row = y / RASTER_ROW_HEIGHT;
	const raster_line_t *line = &raster[frame.s->row_map[row]];
	uint sr = raster_stored_row(frame.cols, y % RASTER_ROW_HEIGHT);
	raster_compose(px, line, frame.cols, sr, row == frame.cursor_row ? frame.cursor_col : -1,
		frame.palette);
}

// The DMA interrupt (core 1) wants line y's pixels: the next in order.
static const uint32_t *__not_in_flash_func(next_line)(unsigned y) {
	(void)y;
	uint32_t seq = requested + 1;
	requested = seq;
	if ((int32_t)(composed - seq) <= 0)
		++late_lines; // shows whatever the buffer holds
	return line_buf[seq % LINE_BUFS];
}

static void __not_in_flash_func(core1_main)(void) {
	diag_core1_stage = 1;
	if (VIDEO_DIAG) {
		// Free-running 24-bit down-counter at the core clock, for timing.
		systick_hw->rvr = 0xffffff;
		systick_hw->cvr = 0;
		systick_hw->csr = 0x5; // enable, processor clock, no interrupt
	}

	// The first lines are ready before the output starts.
	uint32_t next = 0;
	while ((int32_t)(next - requested) <= AHEAD) {
		compose(next);
		composed = ++next;
	}
	diag_core1_stage = 2;
	hstx_dvi_start(next_line);
	diag_core1_stage = 3;

	for (;;) {
		diag_core1_stage = 4;
		while ((int32_t)(next - requested) > AHEAD)
			tight_loop_contents();
		uint32_t start = VIDEO_DIAG ? systick_hw->cvr : 0;
		compose(next);
		if (VIDEO_DIAG) {
			uint32_t cycles = (start - systick_hw->cvr) & 0xffffff;
			if (cycles > diag_max_cycles)
				diag_max_cycles = cycles;
		}
		composed = ++next;
		if (next % RASTER_ACTIVE_HEIGHT == 0)
			diag_frames = next / RASTER_ACTIVE_HEIGHT;
	}
}

void video_init(void) {
	hstx_dvi_init();
	diag_reinit(115200);
	diag_mark("clocks and HSTX set up");
}

void video_set_palette(const uint16_t colours[PALETTE_SIZE]) {
	uint8_t t = palette_next;
	if (core1_launched) {
		// Until core 1 has taken the last table, the other may still be
		// in use.
		while (palette_used != t)
			tight_loop_contents();
		t ^= 1;
	}
	for (uint c = 0; c < PALETTE_SIZE; ++c)
		palette_words[t][c] = colours[c] | (uint32_t)colours[c] << 16;
	__dmb(); // the table before core 1 can pick it
	palette_next = t;
}

void video_update(screen_t *screen) {
	// Blink: flip the phase every BLINK_HALF_PERIOD_MS and re-render the lines
	// that have blinking cells. (The cursor is core 1's, from the screen.)
	static bool blink_off;
	static uint32_t blink_slots;
	static absolute_time_t next_blink;
	if (time_reached(next_blink)) {
		next_blink = make_timeout_time_ms(BLINK_HALF_PERIOD_MS);
		blink_off = !blink_off;
		screen->dirty |= blink_slots;
	}

	// Display row of each slot, for reverse video (DECSCNM) by row.
	uint8_t slot_row[SCREEN_ROWS];
	for (uint row = 0; row < SCREEN_ROWS; ++row)
		slot_row[screen->row_map[row]] = (uint8_t)row;

	uint32_t dirty = screen->dirty;
	screen->dirty = 0;
	if (splash_holds_raster) {
		// Keep those for when the splash ends.
		uint32_t held = dirty & ((1u << SPLASH_SLOTS) - 1);
		screen->dirty |= held;
		dirty &= ~held;
	}
	for (int slot = 0; dirty; ++slot, dirty >>= 1) {
		if (!(dirty & 1))
			continue;
		unsigned flags = (blink_off ? RASTER_BLINK_OFF : 0) |
			(screen->reverse_rows >> slot_row[slot] & 1 ? RASTER_REVERSE : 0);
		bool blinks = raster_render_line(&raster[slot], &screen->lines[slot], screen->cols, flags);
		if (blinks)
			blink_slots |= 1u << slot;
		else
			blink_slots &= ~(1u << slot);
	}
}

video_stats_t video_stats(void) {
	const hstx_timing_t *t = &hstx_timing;
	video_stats_t st = {
		.frames = diag_frames,
		.max_line_cycles = diag_max_cycles,
		.late = late_lines,
		.core1_stage = diag_core1_stage,
		// The system clock is 10 x the pixel clock.
		.cycles_budget = 10u * (t->h_front + t->h_sync + t->h_back + t->h_active),
	};
	diag_max_cycles = 0;
	return st;
}

void video_start(screen_t *screen) {
	video_screen = screen;
	video_update(screen);
	diag_mark("screen rasterised; launching core 1");
	core1_launched = true;
	multicore_launch_core1(core1_main);
}

// Wait until core 1 has finished a frame after this call, so any frame it
// composes next latched the current state.
static void wait_frame(void) {
	uint32_t frame = diag_frames;
	while (diag_frames == frame)
		tight_loop_contents();
}

bool video_splash(const splash_image_t *img, const splash_tint_t *tint) {
	if (!splash_check(img) || img->size > SPLASH_DATA_MAX)
		return false;
	// Black while the data is copied, so no half-written logo shows.
	splash_state = SPLASH_BLACK;
	wait_frame();
	splash_holds_raster = true;
	// With LOGO_IN_FLASH this reads flash: after nvflash_retime (main.c).
	memcpy(splash_data, img->data, img->size);
	splash_start = splash_decoder_init(img, splash_data);
	for (unsigned l = 0; l < 4; ++l)
		splash_colours[l] = hstx_rgb565(splash_tint_colour(tint, l));
	__dmb(); // all of that before core 1 can see SPLASH_SHOW
	splash_state = SPLASH_SHOW;
	return true;
}

void video_splash_end(void) {
	// Black again until the text rows are rendered back over the splash.
	splash_state = SPLASH_BLACK;
	wait_frame();
	splash_holds_raster = false;
	video_screen->dirty = (1u << SCREEN_ROWS) - 1;
	video_update(video_screen);
	__dmb();
	splash_state = SPLASH_OFF;
}
