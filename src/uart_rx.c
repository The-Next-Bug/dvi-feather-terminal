#include "uart_rx.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

// Single producer (IRQ) and single consumer (main loop), both on core 0.
static uint8_t buffer[UART_RX_BUFFER_SIZE];
static volatile uint32_t head; // written by the IRQ
static volatile uint32_t tail; // written by uart_rx_read
static volatile uart_rx_stats_t stats;
static uart_inst_t *rx_uart;

static void __not_in_flash_func(uart_rx_irq)(void) {
	uart_hw_t *hw = uart_get_hw(rx_uart);
	uint32_t h = head;
	while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
		uint32_t dr = hw->dr;
		// An overrun flags the byte after the ones that were lost; that byte
		// itself is good.
		if (dr & UART_UARTDR_OE_BITS)
			++stats.overruns;
		if (dr & (UART_UARTDR_BE_BITS | UART_UARTDR_PE_BITS | UART_UARTDR_FE_BITS)) {
			++stats.errors;
			continue;
		}
		++stats.received;
		if (h - tail == UART_RX_BUFFER_SIZE) {
			++stats.dropped;
			continue;
		}
		buffer[h % UART_RX_BUFFER_SIZE] = (uint8_t)dr;
		++h;
	}
	head = h;
}

void uart_rx_init(uart_inst_t *uart, unsigned baud, unsigned tx_pin, unsigned rx_pin) {
	rx_uart = uart;
	uart_init(uart, baud);
	uart_set_format(uart, 8, 1, UART_PARITY_NONE);
	uart_set_fifo_enabled(uart, true);
	gpio_set_function(tx_pin, UART_FUNCSEL_NUM(uart, tx_pin));
	gpio_set_function(rx_pin, UART_FUNCSEL_NUM(uart, rx_pin));

	unsigned irq = UART_IRQ_NUM(uart);
	irq_set_exclusive_handler(irq, uart_rx_irq);
	irq_set_enabled(irq, true);
	uart_set_irqs_enabled(uart, true, false);
}

size_t uart_rx_read(uint8_t *buf, size_t max) {
	uint32_t t = tail, available = head - t;
	size_t n = available < max ? available : max;
	for (size_t i = 0; i < n; ++i)
		buf[i] = buffer[(t + i) % UART_RX_BUFFER_SIZE];
	// Make sure the copy is done before the IRQ may reuse the space.
	__compiler_memory_barrier();
	tail = t + (uint32_t)n;
	return n;
}

uart_rx_stats_t uart_rx_stats(void) {
	uint32_t saved = save_and_disable_interrupts();
	uart_rx_stats_t s = stats;
	restore_interrupts(saved);
	return s;
}
