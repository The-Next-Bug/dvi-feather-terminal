#ifndef HSTX_DVI_H
#define HSTX_DVI_H

// DVI output from the RP2350 HSTX: its command expander and TMDS encoder
// turn RGB565 scanlines into the three TMDS lanes, so the CPU only supplies
// pixels. The pins are GP12-19 (the only HSTX pins), in the order and
// polarity the board header gives (boards/, src/board/board.h).
//
// Two DMA channels take turns (each chains to the other); the DMA interrupt
// reloads the one that just finished with the next piece of the frame: a
// blanking line's command list, or an active line's command list and then
// its pixels, which it asks the line callback for. The interrupt runs on the
// core that calls hstx_dvi_start.
//
// The HSTX shifts out two bits per clk_hstx cycle, so a pixel (10 bits a
// lane) takes 5 cycles: clk_hstx is 5 x the pixel clock, half of PLL_SYS.
// clk_sys comes from PLL_USB instead (bd cpx.1, cpx.4), at the board's
// BOARD_SYS_KHZ whatever the mode: clk_usb and clk_adc divide it down to
// 48 MHz, clk_peri to <= 150 MHz.

#include <stdbool.h>
#include <stdint.h>

// Video timing, by the width DVI_WIDTH (the build option DVI_MODE):
//   800:  800x600 60 Hz, 40 MHz (libdvi's timing, as PicoDVI used): clk_hstx 200 MHz
//   1024: 1024x600 60 Hz reduced blanking, 44 MHz (CVT-RB): clk_hstx 220 MHz
// (bd cpx.7: the 56 Hz and reduced-blanking 800x600 modes, which only
// lowered clk_sys while it was 10 x the pixel clock, are retired.) The
// terminal draws 1024 with its 1024-wide layouts (RASTER_ACTIVE_WIDTH).
#ifndef DVI_WIDTH
#define DVI_WIDTH 1024
#endif
#define HSTX_ACTIVE_WIDTH DVI_WIDTH
#define HSTX_ACTIVE_HEIGHT 600

typedef struct {
	uint16_t h_front, h_sync, h_back, h_active;
	uint16_t v_front, v_sync, v_back, v_active;
	bool h_positive, v_positive; // sync pulse polarity
	uint32_t pll_khz;           // PLL_SYS: 10 x the pixel clock, clk_hstx its half
	uint32_t sys_khz;           // clk_sys: PLL_USB (a multiple of 48 MHz)
} hstx_timing_t;

// Not const: const data stays in flash, which the interrupt must not read.
extern hstx_timing_t hstx_timing;

// Bytes in one active line of RGB565 pixels.
#define HSTX_LINE_BYTES(t) ((t)->h_active * 2u)

// Raise the core voltage, set the PLLs, clk_sys (hstx_timing.sys_khz) and clk_hstx,
// and set up the HSTX, its pins and the DMA channels (not yet running). Call
// before any peripheral that depends on clk_sys/clk_peri (e.g. the UART).
void hstx_dvi_init(void);

// Returns active line y's pixels (HSTX_LINE_BYTES, word aligned), which must
// stay unchanged until the DMA has read them (until the callback is asked for
// line y + 2). Called from the DMA interrupt, once per active line, in
// order; y == 0 starts a frame. Must be quick and must not touch flash.
typedef const uint32_t *(*hstx_line_fn)(unsigned y);

// Start the output, with the interrupt on this core.
void hstx_dvi_start(hstx_line_fn line);

// Stop the output (the DMA, then the HSTX once its FIFO has drained) and
// leave the pins undriven: no signal, as with the cable pulled, so the
// monitor may power down. Then hstx_dvi_resume starts it again from a
// frame start, asking the line callback for line 0 first, and connects the
// pins once whole lines go out; the monitor has to resync. Both on the
// core that called hstx_dvi_start, from outside its interrupt.
void hstx_dvi_stop(void);
void hstx_dvi_resume(void);

// How long a frame lasts, in microseconds.
uint32_t hstx_dvi_frame_us(void);

// Frames started since hstx_dvi_start.
extern volatile uint32_t hstx_dvi_frames;

// RGB222 (0bRRGGBB, as in screen.h) to RGB565, each 2-bit level repeated
// through the wider field so level 3 is full scale.
static inline uint16_t hstx_rgb565(uint8_t rgb222) {
	unsigned r = rgb222 >> 4 & 3, g = rgb222 >> 2 & 3, b = rgb222 & 3;
	unsigned r5 = r << 3 | r << 1 | r >> 1, g6 = g * 0x15u, b5 = b << 3 | b << 1 | b >> 1;
	return (uint16_t)(r5 << 11 | g6 << 5 | b5);
}

#endif
