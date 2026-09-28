#include "video.h"

#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/systick.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"

#include "diag.h"
#include "raster.h"

#define FRAME_WIDTH RASTER_ACTIVE_WIDTH
#define FRAME_HEIGHT RASTER_ACTIVE_HEIGHT
#define DVI_TIMING dvi_timing_800x600p_60hz

#define PLANE_WORDS (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD)
// The encoder writes RASTER_WIDTH pixels per plane, past the end of the
// line: each plane's excess lands on the start of the next (which is then
// overwritten) and the last plane's in this slack.
#define SLACK_WORDS ((RASTER_WIDTH - FRAME_WIDTH) / DVI_SYMBOLS_PER_WORD)
#define TMDS_BUF_WORDS (RASTER_PLANES * PLANE_WORDS + SLACK_WORDS)

_Static_assert(SCREEN_ROWS * RASTER_ROW_HEIGHT == FRAME_HEIGHT, "rows fill the screen");

// Cursor blink: on for 32 frames, off for 32 (about 1 Hz at 60 Hz).
#define CURSOR_BLINK_FRAMES 32
// Blinking text (SGR 5): shown for this long, then hidden for as long.
#define BLINK_HALF_PERIOD_MS 500

static struct dvi_inst dvi0;
static volatile uint32_t diag_frames, diag_max_cycles;
// Core 1 progress: 1 started, 2 starting DVI, 3 DVI started, 4 in frame loop.
static volatile uint32_t diag_core1_stage;
static screen_t *video_screen;

// One raster line per screen line slot (indexed like screen_t.lines).
static raster_line_t raster[SCREEN_ROWS];

// Everything core 1 reads lives in SRAM (not const, which would put it in
// flash): an XIP stall would starve the serialisers.
//
// The encoder looks each raster byte up in a "font" row; this identity row
// makes each byte its own 8-pixel pattern.
static uint8_t identity_font[256];
// Cursor row copies; the 4 bytes past a stored row are read but not shown.
static uint8_t __attribute__((aligned(4))) cursor_bits[RASTER_PLANES][RASTER_WIDTH / 8];

static void __not_in_flash_func(encode_scanline)(uint32_t *tmdsbuf, const uint8_t *const bits[RASTER_PLANES],
		const uint32_t *const colour[RASTER_PLANES]) {
	// In plane order: each plane's excess is overwritten by the next.
	for (int plane = 0; plane < RASTER_PLANES; ++plane)
		tmds_encode_font_2bpp(bits[plane], colour[plane], tmdsbuf + plane * PLANE_WORDS,
			RASTER_WIDTH, identity_font);
}

static void __not_in_flash_func(core1_main)(void) {
	diag_core1_stage = 1;
	if (VIDEO_DIAG) {
		// Free-running 24-bit down-counter at the core clock, for timing.
		systick_hw->rvr = 0xffffff;
		systick_hw->cvr = 0;
		systick_hw->csr = 0x5; // enable, processor clock, no interrupt
	}

	diag_core1_stage = 2;
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	diag_core1_stage = 3;

	const uint8_t *bits[RASTER_PLANES];
	const uint32_t *colour[RASTER_PLANES];
	for (uint frame = 0;; ++frame) {
		diag_core1_stage = 4;
		// Latch the mode and cursor once per frame so they cannot tear
		// between rows.
		const screen_t *s = video_screen;
		uint cols = s->cols;
		bool cursor_on = s->cursor_visible && !(frame / CURSOR_BLINK_FRAMES & 1);
		uint cursor_row = s->cursor_row;
		uint cursor_x = raster_x_offset(cols) + s->cursor_col * raster_cell_width(cols);
		uint cursor_byte = cursor_x / 8;
		uint cursor_mask = ((1u << raster_cell_width(cols)) - 1) << cursor_x % 8;

		uint row = 0, sy = 0;
		for (uint y = 0; y < FRAME_HEIGHT; ++y) {
			const raster_line_t *line = &raster[s->row_map[row]];
			uint sr = raster_stored_row(cols, sy);
			for (int p = 0; p < RASTER_PLANES; ++p) {
				bits[p] = line->bits[sr][p];
				colour[p] = line->colour[p];
			}
			if (cursor_on && row == cursor_row) {
				// Invert the cursor cell: each pixel swaps between its group's
				// two levels.
				for (int p = 0; p < RASTER_PLANES; ++p) {
					const uint32_t *src = (const uint32_t *)bits[p];
					uint32_t *dst = (uint32_t *)cursor_bits[p];
					for (int i = 0; i < RASTER_BYTES / 4; ++i)
						dst[i] = src[i];
					cursor_bits[p][cursor_byte] ^= (uint8_t)cursor_mask;
					cursor_bits[p][cursor_byte + 1] ^= (uint8_t)(cursor_mask >> 8);
					bits[p] = cursor_bits[p];
				}
			}
			if (++sy == RASTER_ROW_HEIGHT) {
				sy = 0;
				++row;
			}

			uint32_t *tmdsbuf;
			queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
			uint32_t start = VIDEO_DIAG ? systick_hw->cvr : 0;
			encode_scanline(tmdsbuf, bits, colour);
			if (VIDEO_DIAG) {
				uint32_t cycles = (start - systick_hw->cvr) & 0xffffff;
				if (cycles > diag_max_cycles)
					diag_max_cycles = cycles;
			}
			queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
		}
		diag_frames = frame + 1;
	}
}

void video_init(void) {
	// 800x600p60 needs a 400 MHz system clock, which needs 1.3 V. The whole
	// program runs from SRAM (COPY_TO_RAM): flash cannot keep up at 400 MHz.
	vreg_set_voltage(VREG_VOLTAGE_1_30);
	sleep_ms(10);
	diag_mark("vreg 1.30 V; setting system clock");
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
	diag_reinit(115200);
	diag_mark("system clock set; dvi_init");

	for (int i = 0; i < 256; ++i)
		identity_font[i] = (uint8_t)i;

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
	// Swap libdvi's TMDS buffers for ones with slack for the encoder. These
	// are allocated in the same heap space rather than static: dvi_init
	// mallocs its own first (3 x 4.8 KB), so static copies would need that
	// much heap on top, and the RP2040 does not have it (malloc failing
	// panics at boot, silently outside VIDEO_DIAG builds).
	for (int i = 0; i < DVI_N_TMDS_BUFFERS; ++i) {
		uint32_t *buf;
		queue_remove_blocking_u32(&dvi0.q_tmds_free, &buf);
		free(buf);
	}
	for (int i = 0; i < DVI_N_TMDS_BUFFERS; ++i) {
		uint32_t *buf = malloc(TMDS_BUF_WORDS * sizeof(uint32_t));
		queue_add_blocking_u32(&dvi0.q_tmds_free, &buf);
	}
	diag_mark("dvi_init done");
}

void video_update(screen_t *screen) {
	// The cursor cell must be rasterised with its fg level (see
	// raster_render_line). Re-render the lines involved whenever the cursor,
	// or the slot shown on its row, changes.
	static int drawn_slot = -1, drawn_col = -1;
	int slot_now = -1, col_now = -1;
	if (screen->cursor_visible) {
		slot_now = screen->row_map[screen->cursor_row];
		col_now = screen->cursor_col;
	}
	if (slot_now != drawn_slot || col_now != drawn_col) {
		if (drawn_slot >= 0)
			screen->dirty |= 1u << drawn_slot;
		if (slot_now >= 0)
			screen->dirty |= 1u << slot_now;
		drawn_slot = slot_now;
		drawn_col = col_now;
	}

	// Blink: flip the phase every BLINK_HALF_PERIOD_MS and re-render the lines
	// that have blinking cells.
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
	for (int slot = 0; dirty; ++slot, dirty >>= 1) {
		if (!(dirty & 1))
			continue;
		unsigned flags = (blink_off ? RASTER_BLINK_OFF : 0) |
			(screen->reverse_rows >> slot_row[slot] & 1 ? RASTER_REVERSE : 0);
		bool blinks = raster_render_line(&raster[slot], &screen->lines[slot], screen->cols,
			slot == slot_now ? col_now : -1, flags);
		if (blinks)
			blink_slots |= 1u << slot;
		else
			blink_slots &= ~(1u << slot);
	}
}

video_stats_t video_stats(void) {
	video_stats_t st = {
		.frames = diag_frames,
		.max_line_cycles = diag_max_cycles,
		.late = dvi0.late_scanline_ctr,
		.core1_stage = diag_core1_stage,
		// The system clock is the TMDS bit clock: 10 cycles per pixel.
		.cycles_budget = 10u * (DVI_TIMING.h_front_porch + DVI_TIMING.h_sync_width +
			DVI_TIMING.h_back_porch + DVI_TIMING.h_active_pixels),
	};
	diag_max_cycles = 0;
	return st;
}

void video_start(screen_t *screen) {
	video_screen = screen;
	video_update(screen);
	diag_mark("screen rasterised; launching core 1");
	// Core 1 must never stall on the bus while feeding the serialisers.
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);
}
