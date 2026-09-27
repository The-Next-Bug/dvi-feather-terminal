// DVI serial terminal for the Adafruit Feather RP2040 DVI.
//
// Core 1 renders the screen over DVI (video.c). Core 0 receives UART0 into
// a ring buffer (uart_rx.c) and feeds it to a teletype (term.c) that owns
// rows 0-28; row 29 is a status line.

#include <stdio.h>

#include "pico/stdlib.h"

#include "screen.h"
#include "term.h"
#include "term_build.h"
#include "uart_rx.h"
#include "video.h"

#define BAUD 115200

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 0)
#define STATUS_FG RGB222(3, 3, 3)
#define STATUS_BG RGB222(0, 0, 2)

#define TERM_ROWS (SCREEN_ROWS - 1)
#define STATUS_ROW (SCREEN_ROWS - 1)
#define STATUS_INTERVAL_MS 250

static screen_t screen;
static term_t term;

static void draw_status(const uart_rx_stats_t *st) {
	char text[SCREEN_COLS + 1];
	snprintf(text, sizeof text, " %s | UART0 %u 8N1 | rx %lu  ovr %lu  drop %lu  err %lu",
		TERM_BUILD_ID, BAUD, (unsigned long)st->received, (unsigned long)st->overruns,
		(unsigned long)st->dropped, (unsigned long)st->errors);
	screen_clear_line(&screen, STATUS_ROW, STATUS_FG, STATUS_BG);
	screen_puts(&screen, 0, STATUS_ROW, text, STATUS_FG, STATUS_BG);
}

int main(void) {
	video_init();
	// After video_init: the baud divisor depends on the final clk_peri.
	uart_rx_init(uart0, BAUD, PICO_DEFAULT_UART_TX_PIN, PICO_DEFAULT_UART_RX_PIN);

	screen_init(&screen, FG, BG);
	term_init(&term, &screen, 0, TERM_ROWS, FG, BG);
	uart_rx_stats_t shown = uart_rx_stats();
	draw_status(&shown);
	video_start(&screen);

	char banner[SCREEN_COLS + 1];
	snprintf(banner, sizeof banner, "DVI Feather Terminal build %s (%s, %s)\r\n",
		TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
	term_puts(&term, banner);
	term_puts(&term, "UART0 on TX (GP0) / RX (GP1)\r\n\r\n");

	uint8_t buf[256];
	absolute_time_t next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
	while (true) {
		size_t n = uart_rx_read(buf, sizeof buf);
		if (n)
			term_write(&term, buf, n);

		if (time_reached(next_status)) {
			next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
			uart_rx_stats_t st = uart_rx_stats();
			if (st.received != shown.received || st.overruns != shown.overruns ||
			    st.dropped != shown.dropped || st.errors != shown.errors) {
				shown = st;
				draw_status(&shown);
			}
		}
	}
}
