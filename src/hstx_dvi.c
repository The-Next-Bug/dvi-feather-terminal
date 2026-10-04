// HSTX DVI output. The HSTX setup, the command lists and the DMA ping-pong
// follow Raspberry Pi's pico-examples hstx/dvi_out_hstx_encoder
// (BSD-3-Clause, Copyright (c) 2024 Raspberry Pi (Trading) Ltd.).

#include "hstx_dvi.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/vreg.h"

hstx_timing_t hstx_timing =
#if HSTX_MODE == 0
	// What libdvi's dvi_timing_800x600p_60hz sends (and monitors took from
	// PicoDVI): VESA's except a 44-pixel front porch and negative syncs.
	{ 44, 128, 88, 800, 1, 4, 23, 600, false, false, 400000 };
#elif HSTX_MODE == 1
	// VESA 800x600 56 Hz, 36 MHz.
	{ 24, 72, 128, 800, 1, 2, 22, 600, true, true, 360000 };
#elif HSTX_MODE == 2
	// CVT reduced blanking 800x600 60 Hz (+hsync, -vsync), 35.5 MHz
	// nominally; 35.4 MHz is the nearest clk_sys / 10 the PLL makes.
	{ 48, 32, 80, 800, 3, 4, 11, 600, true, false, 354000 };
#else
#error "unknown HSTX_MODE"
#endif

volatile uint32_t hstx_dvi_frames;

// TMDS control symbols for C1 C0 = 00, 01, 10, 11. Lane 0 carries vsync
// (C1) and hsync (C0); the other lanes send 00.
static uint16_t tmds_ctrl[4] = { 0x354, 0x0ab, 0x154, 0x2ab };

#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS (0x2u << 12)
#define HSTX_CMD_NOP (0xfu << 12)

// Command lists, built for the timing by hstx_dvi_init. A blanking line is
// raw control symbols; an active line is its blanking then a TMDS command
// for the pixels, which follow as the next DMA transfer. The NOPs pad them
// to the FIFO's depth (8 words) or more, so two DMA completions never come
// close enough together to be taken as one interrupt.
#define BLANK_WORDS 7
#define ACTIVE_WORDS 9
static uint32_t blank_list[2][BLANK_WORDS]; // [in the vsync pulse]
static uint32_t active_list[ACTIVE_WORDS];

// Interrupt state; everything it reads is in SRAM.
static uint dma_ch[2];
static uint next_ch;     // index into dma_ch of the channel to reload next
static uint line;        // line of the frame the next reload is for
static bool pixels_next; // within an active line: its pixels are next
static uint v_sync_start, v_sync_end, v_active_start, v_total, pixel_words;
static hstx_line_fn line_fn;
static uint32_t csr_on; // the HSTX's CSR while it runs (hstx_dvi_init)

static uint32_t sync_symbols(bool vsync, bool hsync) {
	return tmds_ctrl[vsync << 1 | hsync] | (uint32_t)tmds_ctrl[0] << 10 | (uint32_t)tmds_ctrl[0] << 20;
}

static void build_lists(const hstx_timing_t *t) {
	// Sync signal levels outside and during each pulse.
	bool h_idle = !t->h_positive, h_pulse = t->h_positive;
	bool v_idle = !t->v_positive, v_pulse = t->v_positive;
	for (int pulse = 0; pulse < 2; ++pulse) {
		bool v = pulse ? v_pulse : v_idle;
		uint32_t *l = blank_list[pulse];
		l[0] = HSTX_CMD_RAW_REPEAT | t->h_front;
		l[1] = sync_symbols(v, h_idle);
		l[2] = HSTX_CMD_RAW_REPEAT | t->h_sync;
		l[3] = sync_symbols(v, h_pulse);
		l[4] = HSTX_CMD_RAW_REPEAT | (t->h_back + t->h_active);
		l[5] = sync_symbols(v, h_idle);
		l[6] = HSTX_CMD_NOP;
	}
	uint32_t *l = active_list;
	l[0] = HSTX_CMD_RAW_REPEAT | t->h_front;
	l[1] = sync_symbols(v_idle, h_idle);
	l[2] = HSTX_CMD_NOP;
	l[3] = HSTX_CMD_RAW_REPEAT | t->h_sync;
	l[4] = sync_symbols(v_idle, h_pulse);
	l[5] = HSTX_CMD_NOP;
	l[6] = HSTX_CMD_RAW_REPEAT | t->h_back;
	l[7] = sync_symbols(v_idle, h_idle);
	l[8] = HSTX_CMD_TMDS | t->h_active;
}

// Reload the channel that just finished with what follows the one now
// running. A frame is front porch, vsync, back porch, then active lines.
static void __not_in_flash_func(dma_irq)(void) {
	uint ch = dma_ch[next_ch];
	dma_hw->intr = 1u << ch;
	next_ch ^= 1;
	dma_channel_hw_t *c = &dma_hw->ch[ch];
	if (line < v_active_start) {
		c->read_addr = (uintptr_t)blank_list[line >= v_sync_start && line < v_sync_end];
		c->transfer_count = BLANK_WORDS;
	} else if (!pixels_next) {
		c->read_addr = (uintptr_t)active_list;
		c->transfer_count = ACTIVE_WORDS;
		pixels_next = true;
		return;
	} else {
		uint y = line - v_active_start;
		if (y == 0)
			++hstx_dvi_frames;
		c->read_addr = (uintptr_t)line_fn(y);
		c->transfer_count = pixel_words;
		pixels_next = false;
	}
	if (++line == v_total)
		line = 0;
}

// Channel i starts on blanking line i (0 or 1) and chains to the other; the
// interrupt continues with line 2: the start of a frame.
static void __not_in_flash_func(setup_dma)(void) {
	for (uint i = 0; i < 2; ++i) {
		dma_channel_config c = dma_channel_get_default_config(dma_ch[i]);
		channel_config_set_chain_to(&c, dma_ch[i ^ 1]);
		channel_config_set_dreq(&c, DREQ_HSTX);
		dma_channel_configure(dma_ch[i], &c, &hstx_fifo_hw->fifo,
			blank_list[i >= v_sync_start && i < v_sync_end], BLANK_WORDS, false);
	}
	next_ch = 0;
	line = 2;
	pixels_next = false;
}

void hstx_dvi_init(void) {
	const hstx_timing_t *t = &hstx_timing;
	// 10 x the pixel clock needs more than the default core voltage. The
	// whole program runs from SRAM (COPY_TO_RAM): flash cannot keep up.
	vreg_set_voltage(VREG_VOLTAGE_1_30);
	sleep_ms(10);
	set_sys_clock_khz(t->sys_khz, true);
	// clk_hstx runs undivided from clk_sys after boot; the HSTX wants half.
	clock_configure_int_divider(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
		t->sys_khz * 1000u, 2);

	build_lists(t);
	v_sync_start = t->v_front;
	v_sync_end = v_sync_start + t->v_sync;
	v_active_start = v_sync_end + t->v_back;
	v_total = v_active_start + t->v_active;
	pixel_words = t->h_active / 2;

	// RGB565, two pixels per word, the first in the low half. Each lane
	// takes the top NBITS + 1 bits of the rotated pixel: red (lane 2) is
	// bits 15:11, green (lane 1) 10:5, blue (lane 0) 4:0.
	hstx_ctrl_hw->expand_tmds =
		4u << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | 8u << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
		5u << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | 3u << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
		4u << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | 29u << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
	// Pixels: two 16-bit shifts per word; control symbols: the whole word.
	hstx_ctrl_hw->expand_shift =
		2u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB | 16u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
		1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB | 0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
	// A 10-bit symbol per 5 clk_hstx cycles, 2 bits a cycle (DDR); the clock
	// lane's period is the same 5 cycles.
	csr_on = HSTX_CTRL_CSR_EXPAND_EN_BITS | 5u << HSTX_CTRL_CSR_CLKDIV_LSB | 5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
		2u << HSTX_CTRL_CSR_SHIFT_LSB | HSTX_CTRL_CSR_EN_BITS;
	hstx_ctrl_hw->csr = 0;
	hstx_ctrl_hw->csr = csr_on;

	// HSTX bit n is GPIO 12 + n. Clock on bits 2/3; lanes 0, 1, 2 (D0-D2)
	// on bits 0/1, 6/7, 4/5. Each lane's even shift-register bit goes out in
	// the first half of the cycle, the odd one in the second; the - pin is
	// the + pin inverted.
	hstx_ctrl_hw->bit[2] = HSTX_CTRL_BIT0_CLK_BITS;
	hstx_ctrl_hw->bit[3] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
	static const uint8_t lane_bit[3] = { 0, 6, 4 };
	for (uint lane = 0; lane < 3; ++lane) {
		uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB | (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
		hstx_ctrl_hw->bit[lane_bit[lane]] = sel;
		hstx_ctrl_hw->bit[lane_bit[lane] + 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
	}
	for (uint gpio = 12; gpio <= 19; ++gpio) {
		gpio_set_function(gpio, GPIO_FUNC_HSTX);
		// As PicoDVI set them: 2 mA, slew limited, input off. Enough at
		// these rates, and the 3V3 regulator stays cool.
		hw_write_masked(&pads_bank0_hw->io[gpio], PADS_BANK0_GPIO0_DRIVE_VALUE_2MA << PADS_BANK0_GPIO0_DRIVE_LSB,
			PADS_BANK0_GPIO0_DRIVE_BITS | PADS_BANK0_GPIO0_SLEWFAST_BITS | PADS_BANK0_GPIO0_IE_BITS);
	}

	for (int i = 0; i < 2; ++i)
		dma_ch[i] = (uint)dma_claim_unused_channel(true);
	setup_dma();
}

void hstx_dvi_start(hstx_line_fn fn) {
	line_fn = fn;
	// The DMA feeds the HSTX FIFO; it must not wait behind the cores.
	bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
	irq_set_exclusive_handler(DMA_IRQ_0, dma_irq);
	irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
	for (int i = 0; i < 2; ++i)
		dma_channel_set_irq0_enabled(dma_ch[i], true);
	irq_set_enabled(DMA_IRQ_0, true);
	dma_channel_start(dma_ch[0]);
}

void __not_in_flash_func(hstx_dvi_stop)(void) {
	uint32_t mask = 1u << dma_ch[0] | 1u << dma_ch[1];
	irq_set_enabled(DMA_IRQ_0, false);
	// Disabled first, so aborting one does not start the other (chain).
	for (int i = 0; i < 2; ++i)
		hw_clear_bits(&dma_hw->ch[dma_ch[i]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
	dma_hw->abort = mask;
	while (dma_hw->abort & mask)
		tight_loop_contents();
	dma_hw->intr = mask;
	// What is in the FIFO goes out (the HSTX does not pop it while off):
	// a few lines' time at most, as one word can repeat a symbol a line long.
	uint64_t until = time_us_64() + 200;
	while (!(hstx_fifo_hw->stat & HSTX_FIFO_STAT_EMPTY_BITS) && time_us_64() < until)
		tight_loop_contents();
	// No clock on the clock lane: the monitor sees no signal.
	hstx_ctrl_hw->csr = 0;
}

void __not_in_flash_func(hstx_dvi_resume)(void) {
	setup_dma();
	hstx_ctrl_hw->csr = csr_on;
	dma_hw->intr = 1u << dma_ch[0] | 1u << dma_ch[1];
	irq_clear(DMA_IRQ_0);
	irq_set_enabled(DMA_IRQ_0, true);
	dma_channel_start(dma_ch[0]);
}

uint32_t hstx_dvi_frame_us(void) {
	const hstx_timing_t *t = &hstx_timing;
	uint32_t pixels = (uint32_t)(t->h_front + t->h_sync + t->h_back + t->h_active) *
		(t->v_front + t->v_sync + t->v_back + t->v_active);
	return (uint32_t)(pixels * 10000ull / t->sys_khz); // clk_sys is 10 x the pixel clock
}
