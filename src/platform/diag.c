#include "diag.h"

#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "hardware/structs/sio.h"
#include "hardware/uart.h"

#include "board.h"

#define DIAG_UART BOARD_UART_INST
#define DIAG_BAUD 115200

// stdio on UART0 TX, blocking, so panic() messages (TinyUSB's driver panics
// on USB errors) are seen; panic then ends in a breakpoint, which without a
// debugger is a HardFault, reported below.
static void diag_out_chars(const char *buf, int len) {
	uart_write_blocking(DIAG_UART, (const uint8_t *)buf, (size_t)len);
}

static void led_toggle(void) {
#ifdef PICO_DEFAULT_LED_PIN
	gpio_xor_mask64(1ull << PICO_DEFAULT_LED_PIN);
#endif
}

static stdio_driver_t diag_stdio = {
	.out_chars = diag_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	.crlf_enabled = true,
#endif
};

void diag_init(void) {
	if (!VIDEO_DIAG) {
		return;
	}
	uart_init(DIAG_UART, DIAG_BAUD);
	gpio_set_function(BOARD_UART_TX_PIN, UART_FUNCSEL_NUM(DIAG_UART, BOARD_UART_TX_PIN));
#ifdef PICO_DEFAULT_LED_PIN
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif
	uart_puts(DIAG_UART, "\r\n\r\ndiag: boot\r\n");
	stdio_set_driver_enabled(&diag_stdio, true);
}

void diag_reinit(uint32_t baud) {
	if (VIDEO_DIAG) {
		uart_set_baudrate(DIAG_UART, baud);
	}
}

void diag_mark(const char *text) {
	if (!VIDEO_DIAG) {
		return;
	}
	uart_puts(DIAG_UART, "diag: ");
	uart_puts(DIAG_UART, text);
	uart_puts(DIAG_UART, "\r\n");
	// Let it all go out: the next step may change the clock or reset the UART.
	uart_tx_wait_blocking(DIAG_UART);
}

void diag_heartbeat(void) {
	if (VIDEO_DIAG) {
		led_toggle();
	}
}

#if VIDEO_DIAG
static void put_hex(uint32_t v) {
	static const char hex[] = "0123456789abcdef";
	for (int shift = 28; shift >= 0; shift -= 4) {
		uart_putc_raw(DIAG_UART, hex[v >> shift & 0xf]);
	}
}

// HardFault: report the faulting core, PC and LR from the exception frame,
// then blink the LED fast forever. Replaces the SDK's weak handler.
void __used hardfault_report(const uint32_t *frame) {
	uart_puts(DIAG_UART, "\r\ndiag: HARD FAULT core ");
	uart_putc_raw(DIAG_UART, (char)('0' + sio_hw->cpuid));
	uart_puts(DIAG_UART, " pc ");
	put_hex(frame[6]);
	uart_puts(DIAG_UART, " lr ");
	put_hex(frame[5]);
	uart_puts(DIAG_UART, "\r\n");
	for (;;) {
		led_toggle();
		busy_wait_us_32(100000);
	}
}

void __attribute__((naked)) isr_hardfault(void) {
	__asm volatile(
		"movs r0, #4\n"
		"mov r1, lr\n"
		"tst r0, r1\n"
		"beq 1f\n"
		"mrs r0, psp\n"
		"b 2f\n"
		"1: mrs r0, msp\n"
		"2: ldr r1, =hardfault_report\n"
		"bx r1\n");
}
#endif
