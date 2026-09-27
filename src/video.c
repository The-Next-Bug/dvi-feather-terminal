#include "video.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"

#include "font.h"

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define DVI_TIMING dvi_timing_640x480p_60hz

_Static_assert(SCREEN_COLS * FONT_WIDTH == FRAME_WIDTH, "screen width");
_Static_assert(SCREEN_ROWS * FONT_HEIGHT == FRAME_HEIGHT, "screen height");

static struct dvi_inst dvi0;
static const screen_t *video_screen;

static void __not_in_flash_func(core1_main)(void) {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	while (true) {
		for (uint y = 0; y < FRAME_HEIGHT; ++y) {
			const screen_line_t *line = screen_line(video_screen, y / FONT_HEIGHT);
			const uint8_t *font_row = &font_8x16[y % FONT_HEIGHT * FONT_GLYPHS];
			uint32_t *tmdsbuf;
			queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
			for (int plane = 0; plane < SCREEN_PLANES; ++plane) {
				tmds_encode_font_2bpp(line->glyph, line->colour[plane],
					tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
					FRAME_WIDTH, font_row);
			}
			queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
		}
	}
}

void video_init(void) {
	// 640x480p60 needs a 252 MHz system clock; 1.2 V keeps that reliable.
	vreg_set_voltage(VREG_VOLTAGE_1_20);
	sleep_ms(10);
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

void video_start(const screen_t *screen) {
	video_screen = screen;
	// Core 1 must never stall on the bus while feeding the serialisers.
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);
}
