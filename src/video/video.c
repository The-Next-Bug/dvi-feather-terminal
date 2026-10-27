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
_Static_assert(RASTER_ACTIVE_HEIGHT == HSTX_ACTIVE_HEIGHT, "the raster's height is the mode's");
// A mode wider than the raster (1024x600, bd cpx) shows it centered: the
// line buffers' borders stay black (zero from boot, never written).
_Static_assert(HSTX_ACTIVE_WIDTH >= RASTER_ACTIVE_WIDTH && (HSTX_ACTIVE_WIDTH - RASTER_ACTIVE_WIDTH) % 4 == 0,
	"the raster fits the mode on whole words");
#define BORDER_WORDS ((HSTX_ACTIVE_WIDTH - RASTER_ACTIVE_WIDTH) / 4)
#define HSTX_LINE_WORDS (HSTX_ACTIVE_WIDTH / 2)

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
static uint32_t line_buf[LINE_BUFS][HSTX_LINE_WORDS];
// Scanlines are numbered in order from the first one of the first frame.
// Unsigned and compared by difference, so they may wrap (every 33 hours);
// seq % LINE_BUFS survives that, as LINE_BUFS divides 2^32, but
// RASTER_ACTIVE_HEIGHT does not: the frame and the line within it are
// counted apart (frame_no, frame_y), or the picture shifts at the wrap.
static volatile uint32_t requested = UINT32_MAX; // last line handed to the DMA
static volatile uint32_t composed;               // lines composed so far
static volatile uint32_t late_lines;
// Core 1: the frame and the line of it that compose makes next.
static uint32_t frame_no;
static uint frame_y;

// The palette (video_set_palette): index c as an RGB565 pixel in both
// halves of a word. Two tables: core 0 fills the one core 1 is not using,
// and core 1 switches at the start of a frame, so no frame mixes the two.
static uint32_t palette_words[2][PALETTE_SIZE];
static volatile uint8_t palette_next; // core 0: the table for the next frame
static volatile uint8_t palette_used; // core 1: the table of the current frame
static bool core1_launched;
static volatile bool blank_next; // video_set_blank, latched per frame

// diag_frames counts frames in every build: video_splash waits on it.
static volatile uint32_t diag_frames, diag_max_cycles;
// Core 1 progress: 1 started, 2 starting HSTX, 3 HSTX started, 4 in frame loop.
static volatile uint32_t diag_core1_stage;
// Core 0: the longest single line render, the time core 1 may see that
// slot half drawn.
static uint32_t diag_max_render_us;
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
// decoded from splash_start with splash_colors per level.
enum { SPLASH_OFF, SPLASH_BLACK, SPLASH_SHOW };
static volatile uint8_t splash_state;
static splash_decoder_t splash_start;
static uint16_t splash_colors[4];
// Core 0: the splash owns those slots (video_update leaves them alone).
static bool splash_holds_raster;

// What core 1 latched at the start of the frame it is composing: the mode
// and cursor cannot tear between rows.
static struct {
	const screen_t *s;
	bool blank;
	uint8_t splash;
	raster_layout_t layout;
	uint cursor_row;
	int cursor_col; // -1: not shown this frame
	splash_decoder_t decoder;
	uint16_t colors[4];
	const uint32_t *palette;
} frame;

static void __not_in_flash_func(start_frame)(uint32_t n) {
	const screen_t *s = video_screen;
	frame.s = s;
	frame.layout = raster_layout(s->cols);
	bool cursor_on = s->cursor_visible && !(n / CURSOR_BLINK_FRAMES & 1);
	frame.cursor_row = s->cursor_row;
	frame.cursor_col = cursor_on ? (int)s->cursor_col : -1;
	frame.blank = blank_next;
	frame.splash = splash_state;
	uint8_t p = palette_next;
	palette_used = p;
	frame.palette = palette_words[p];
	if (frame.splash == SPLASH_SHOW) {
		frame.decoder = splash_start;
		for (int i = 0; i < 4; ++i) {
			frame.colors[i] = splash_colors[i];
		}
	}
}

static void __not_in_flash_func(compose_line)(uint32_t seq, uint y) {
	if (y == 0) {
		start_frame(frame_no);
	}
	uint32_t *px = line_buf[seq % LINE_BUFS] + BORDER_WORDS;
	if (frame.blank) {
		for (uint i = 0; i < RASTER_LINE_WORDS; ++i) {
			px[i] = 0;
		}
		return;
	}
	if (frame.splash != SPLASH_OFF && y < SPLASH_HEIGHT) {
		if (frame.splash == SPLASH_SHOW) {
			splash_compose((uint16_t *)px, &frame.decoder, y, frame.colors);
		} else {
			for (uint i = 0; i < RASTER_LINE_WORDS; ++i) {
				px[i] = 0;
			}
		}
		return;
	}
	uint row = y / RASTER_ROW_HEIGHT;
	const raster_line_t *line = &raster[frame.s->row_map[row]];
	uint sr = raster_stored_row(frame.layout, y % RASTER_ROW_HEIGHT);
	raster_compose(px, line, frame.layout, sr, row == frame.cursor_row ? frame.cursor_col : -1,
		frame.palette);
}

// Compose line seq: frame line frame_y, then step to the next.
static void __not_in_flash_func(compose)(uint32_t seq) {
	compose_line(seq, frame_y);
	if (++frame_y == RASTER_ACTIVE_HEIGHT) {
		frame_y = 0;
		diag_frames = ++frame_no;
	}
}

// Skip the rest of the frame being composed (none at a frame start).
static uint32_t __not_in_flash_func(to_frame_start)(uint32_t next) {
	if (frame_y) {
		next += RASTER_ACTIVE_HEIGHT - frame_y;
		frame_y = 0;
		diag_frames = ++frame_no;
	}
	return next;
}

// The DMA interrupt (core 1) wants line y's pixels: the next in order.
static const uint32_t *__not_in_flash_func(next_line)(unsigned y) {
	(void)y;
	uint32_t seq = requested + 1;
	requested = seq;
	if ((int32_t)(composed - seq) <= 0) {
		++late_lines; // shows whatever the buffer holds
	}
	return line_buf[seq % LINE_BUFS];
}

// After a wake, the signal goes off once more for RESYNC_OFF_US,
// RESYNC_AFTER_US later: a monitor that woke from a long sleep without
// locking to the signal (no picture until a reboot, bd cpx.5) gets a new
// start, as if the cable were plugged in again. One that did lock only
// locks again.
#define RESYNC_AFTER_US 3000000u
#define RESYNC_OFF_US 500000u

// No DVI signal at all while blanked (video_set_blank), so the monitor can
// power down (its backlight too), and for at least off_us. Frame time goes
// on meanwhile (frames counted, the palette switched), as core 0 waits on
// both; then the output starts again at a frame start. Returns the next
// line to compose.
static uint32_t __not_in_flash_func(signal_off)(uint32_t next, uint32_t off_us) {
	hstx_dvi_stop();
	next = to_frame_start(next);
	const uint32_t frame_us = hstx_dvi_frame_us();
	uint64_t frame_end = time_us_64() + frame_us;
	const uint64_t on_at = time_us_64() + off_us;
	while (blank_next || time_us_64() < on_at) {
		if (time_us_64() >= frame_end) {
			frame_end += frame_us;
			next += RASTER_ACTIVE_HEIGHT;
			diag_frames = ++frame_no;
			palette_used = palette_next;
		}
		tight_loop_contents();
	}
	// As at power-on: the first lines ready before the output starts.
	requested = next - 1;
	composed = next;
	while ((int32_t)(next - requested) <= AHEAD) {
		compose(next);
		composed = ++next;
	}
	hstx_dvi_resume();
	return next;
}

static void __not_in_flash_func(core1_main)(void) {
	diag_core1_stage = 1;
	// Free-running 24-bit down-counter at the core clock (core 1's own
	// SysTick), timing every compose: two register reads a line.
	systick_hw->rvr = 0xffffff;
	systick_hw->cvr = 0;
	systick_hw->csr = 0x5; // enable, processor clock, no interrupt

	// The first lines are ready before the output starts.
	uint32_t next = 0;
	while ((int32_t)(next - requested) <= AHEAD) {
		compose(next);
		composed = ++next;
	}
	diag_core1_stage = 2;
	hstx_dvi_start(next_line);
	diag_core1_stage = 3;

	uint64_t resync_at = 0; // 0: none due
	for (;;) {
		diag_core1_stage = 4;
		if (blank_next) {
			next = signal_off(next, 0);
			resync_at = time_us_64() + RESYNC_AFTER_US;
		} else if (resync_at && time_us_64() >= resync_at) {
			resync_at = 0;
			next = signal_off(next, RESYNC_OFF_US);
		}
		while ((int32_t)(next - requested) > AHEAD) {
			tight_loop_contents();
		}
		// Includes the DMA interrupts taken meanwhile: they share core 1.
		uint32_t start = systick_hw->cvr;
		compose(next);
		uint32_t cycles = (start - systick_hw->cvr) & 0xffffff;
		if (cycles > diag_max_cycles) {
			diag_max_cycles = cycles;
		}
		composed = ++next;
	}
}

void video_init(void) {
	hstx_dvi_init();
	diag_reinit(115200);
	diag_mark("clocks and HSTX set up");
}

void video_set_palette(const uint16_t colors[PALETTE_SIZE]) {
	uint8_t t = palette_next;
	if (core1_launched) {
		// Until core 1 has taken the last table, the other may still be
		// in use.
		while (palette_used != t) {
			tight_loop_contents();
		}
		t ^= 1;
	}
	for (uint c = 0; c < PALETTE_SIZE; ++c) {
		palette_words[t][c] = colors[c] | (uint32_t)colors[c] << 16;
	}
	__dmb(); // the table before core 1 can pick it
	palette_next = t;
}

void video_set_blank(bool blank) {
	blank_next = blank;
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
	for (uint row = 0; row < SCREEN_ROWS; ++row) {
		slot_row[screen->row_map[row]] = (uint8_t)row;
	}

	uint32_t dirty = screen->dirty;
	screen->dirty = 0;
	if (splash_holds_raster) {
		// Keep those for when the splash ends.
		uint32_t held = dirty & ((1u << SPLASH_SLOTS) - 1);
		screen->dirty |= held;
		dirty &= ~held;
	}
	// A new layout's fonts are copied in before any line is timed (a
	// layout change re-renders every line anyway).
	raster_layout_t layout = raster_layout(screen->cols);
	if (dirty) {
		raster_select_font(layout);
	}
	for (int slot = 0; dirty; ++slot, dirty >>= 1) {
		if (!(dirty & 1)) {
			continue;
		}
		unsigned flags = (blink_off ? RASTER_BLINK_OFF : 0) |
			(screen->reverse_rows >> slot_row[slot] & 1 ? RASTER_REVERSE : 0);
		uint32_t start = time_us_32();
		bool blinks = raster_render_line(&raster[slot], &screen->lines[slot], layout, flags);
		uint32_t us = time_us_32() - start;
		if (us > diag_max_render_us) {
			diag_max_render_us = us;
		}
		if (blinks) {
			blink_slots |= 1u << slot;
		} else {
			blink_slots &= ~(1u << slot);
		}
	}
}

video_stats_t video_stats(void) {
	const hstx_timing_t *t = &hstx_timing;
	video_stats_t st = {
		.frames = diag_frames,
		.max_line_cycles = diag_max_cycles,
		.late = late_lines,
		.core1_stage = diag_core1_stage,
		// PLL_SYS is 10 x the pixel clock; clk_sys may be another.
		.cycles_budget = (uint32_t)(10ull * (t->h_front + t->h_sync + t->h_back + t->h_active) * t->sys_khz /
			t->pll_khz),
		.max_render_us = diag_max_render_us,
	};
	diag_max_cycles = 0;
	diag_max_render_us = 0;
	return st;
}

// A scroll reuses slot as display row `row` (screen recycle hook): blank its
// pixels now, in the color the row shows (its fg in reverse video), so the
// line that left does not show in its new place until video_update renders
// the slot. Not while the splash keeps its data in the text slots.
static void recycle_slot(void *ctx, unsigned slot, unsigned row, uint8_t fg, uint8_t bg) {
	const screen_t *s = ctx;
	if (splash_holds_raster && slot < SPLASH_SLOTS) {
		return;
	}
	raster_blank_line(&raster[slot], s->reverse_rows >> row & 1 ? fg : bg);
}

void video_start(screen_t *screen) {
	video_screen = screen;
	screen_set_recycle(screen, recycle_slot, screen);
	video_update(screen);
	diag_mark("screen rasterized; launching core 1");
	core1_launched = true;
	multicore_launch_core1(core1_main);
}

// Wait until core 1 has finished a frame after this call, so any frame it
// composes next latched the current state.
static void wait_frame(void) {
	uint32_t frame = diag_frames;
	while (diag_frames == frame) {
		tight_loop_contents();
	}
}

bool video_splash(const splash_image_t *img, const splash_tint_t *tint) {
	if (!splash_check(img) || img->size > SPLASH_DATA_MAX) {
		return false;
	}
	// Black while the data is copied, so no half-written logo shows.
	splash_state = SPLASH_BLACK;
	wait_frame();
	splash_holds_raster = true;
	// With LOGO_IN_FLASH this reads flash: after nvflash_retime (main.c).
	memcpy(splash_data, img->data, img->size);
	splash_start = splash_decoder_init(img, splash_data);
	for (unsigned l = 0; l < 4; ++l) {
		splash_colors[l] = hstx_rgb565(splash_tint_color(tint, l));
	}
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
