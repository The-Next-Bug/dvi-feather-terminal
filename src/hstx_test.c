// HSTX DVI bring-up test (bd dmt.8.1): a static test pattern from
// hstx_dvi.c, to check on hardware that the HSTX drives the Pico DVI Sock
// at 800x600 (HSTX_MODE) before the terminal moves over to it.
//
// The pattern (800x600):
//   - a 1-pixel white border: every edge pixel should be visible;
//   - rows 1-400: the 64 RGB222 colours, 8 x 8 tiles (index = row * 8 + col,
//     0bRRGGBB), with black gaps;
//   - rows 401-499: a 1-pixel checkerboard (the most TMDS transitions),
//     with a white line moving down through it once per frame: if it moves
//     smoothly, frames are not being dropped;
//   - rows 500-598: the four levels of red, green, blue and grey, left to
//     right, to compare brightness with the PicoDVI build.
//
// UART0 (GP0 TX, 115200 8N1) prints the clocks and the frame count every
// second; the LED blinks (1 s on, 1 s off) while frames are being sent.

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "hstx_dvi.h"
#include "screen.h"

#define W 800
#define H 600
#define WORDS (W / 2)

#define WHITE RGB222(3, 3, 3)
#define BLACK RGB222(0, 0, 0)

#define TILE_TOP 1
#define TILE_ROWS 8
#define TILE_PITCH 50 // 48 lines of colour, 2 of gap
#define CHECK_TOP (TILE_TOP + TILE_ROWS * TILE_PITCH)
#define LEVELS_TOP 500

enum { ROW_WHITE, ROW_GAP, ROW_CHECK0, ROW_CHECK1, ROW_LEVELS, ROW_TILES, ROW_COUNT = ROW_TILES + TILE_ROWS };

// Not const and not in flash: the DMA interrupt reads them.
static uint32_t rows[ROW_COUNT][WORDS];

static void put(uint32_t *row, unsigned x, uint8_t rgb222) {
	uint16_t *px = (uint16_t *)row;
	px[x] = hstx_rgb565(rgb222);
}

static void build_rows(void) {
	for (unsigned x = 0; x < W; ++x) {
		bool edge = x == 0 || x == W - 1;
		put(rows[ROW_WHITE], x, WHITE);
		put(rows[ROW_GAP], x, edge ? WHITE : BLACK);
		put(rows[ROW_CHECK0], x, edge || x & 1 ? WHITE : BLACK);
		put(rows[ROW_CHECK1], x, edge || !(x & 1) ? WHITE : BLACK);
		// 16 blocks of 50 px: red 0-3, green 0-3, blue 0-3, grey 0-3.
		unsigned block = x / 50, level = block & 3, plane = block / 4;
		uint8_t c = plane == 0 ? RGB222(level, 0, 0) : plane == 1 ? RGB222(0, level, 0)
			: plane == 2 ? RGB222(0, 0, level) : RGB222(level, level, level);
		put(rows[ROW_LEVELS], x, edge ? WHITE : c);
		// 8 tiles of 100 px with a 4-px black gap on the left.
		for (unsigned r = 0; r < TILE_ROWS; ++r)
			put(rows[ROW_TILES + r], x, edge ? WHITE : x % 100 < 4 ? BLACK : (uint8_t)(r * 8 + x / 100));
	}
}

static unsigned marker_y;

static const uint32_t *__not_in_flash_func(line)(unsigned y) {
	if (y == 0) {
		marker_y = CHECK_TOP + hstx_dvi_frames % (LEVELS_TOP - CHECK_TOP);
		return rows[ROW_WHITE];
	}
	if (y == H - 1)
		return rows[ROW_WHITE];
	if (y < CHECK_TOP) {
		unsigned t = y - TILE_TOP;
		return t % TILE_PITCH < TILE_PITCH - 2 ? rows[ROW_TILES + t / TILE_PITCH] : rows[ROW_GAP];
	}
	if (y < LEVELS_TOP)
		return y == marker_y ? rows[ROW_WHITE] : rows[y & 1 ? ROW_CHECK1 : ROW_CHECK0];
	return rows[ROW_LEVELS];
}

int main(void) {
	hstx_dvi_init();
	// After the clock change: the UART's divisor depends on clk_peri.
	stdio_init_all();
#ifdef PICO_DEFAULT_LED_PIN
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

	build_rows();
	hstx_dvi_start(line);

	const hstx_timing_t *t = &hstx_timing;
	unsigned h_total = t->h_front + t->h_sync + t->h_back + t->h_active;
	unsigned v_total = t->v_front + t->v_sync + t->v_back + t->v_active;
	uint32_t last = 0;
	for (unsigned s = 0;; ++s) {
		sleep_ms(1000);
		uint32_t frames = hstx_dvi_frames;
		printf("hstx_test mode %d: %ux%u, total %ux%u, clk_sys %lu kHz, clk_hstx %lu kHz (measured), "
			"%lu frames/s, %lu frames\n",
			HSTX_MODE, t->h_active, t->v_active, h_total, v_total,
			(unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS),
			(unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX),
			(unsigned long)(frames - last), (unsigned long)frames);
#ifdef PICO_DEFAULT_LED_PIN
		gpio_put(PICO_DEFAULT_LED_PIN, frames != last && (s & 1));
#endif
		last = frames;
	}
}
