#include "serial.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

// Receive: single producer (IRQ) and single consumer (main loop), both on
// core 0.
static uint8_t rx_buffer[SERIAL_RX_BUFFER_SIZE];
static volatile uint32_t rx_head; // written by the IRQ
static volatile uint32_t rx_tail; // written by serial_read
static volatile bool rx_paused;   // RX interrupts off: ring buffer nearly full
static volatile serial_stats_t stats;
static uart_inst_t *serial_uart;

// Stop draining the FIFO with less room than this left in the ring buffer
// (more than the FIFO holds, so the bytes read in the same IRQ still fit),
// and resume once half the buffer is free.
#define RX_PAUSE_ROOM 64
#define RX_RESUME_ROOM (SERIAL_RX_BUFFER_SIZE / 2)

// Transmit queue: filled and drained by the main loop only.
static uint8_t tx_buffer[SERIAL_TX_BUFFER_SIZE];
static uint32_t tx_head, tx_tail;

static void set_rx_irqs(bool on) {
	uart_hw_t *hw = uart_get_hw(serial_uart);
	if (on)
		hw_set_bits(&hw->imsc, UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS);
	else
		hw_clear_bits(&hw->imsc, UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS);
}

static void __not_in_flash_func(serial_irq)(void) {
	uart_hw_t *hw = uart_get_hw(serial_uart);
	uint32_t h = rx_head;
	while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
		if (SERIAL_RX_BUFFER_SIZE - (h - rx_tail) < RX_PAUSE_ROOM) {
			// Leave the rest in the FIFO: it fills, RTS drops, the host waits.
			set_rx_irqs(false);
			rx_paused = true;
			++stats.throttled;
			break;
		}
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
		rx_buffer[h % SERIAL_RX_BUFFER_SIZE] = (uint8_t)dr;
		++h;
	}
	rx_head = h;
}

void serial_init(uart_inst_t *uart, unsigned baud, unsigned tx_pin, unsigned rx_pin, int cts_pin,
		int rts_pin) {
	serial_uart = uart;
	uart_init(uart, baud);
	uart_set_format(uart, 8, 1, UART_PARITY_NONE);
	uart_set_fifo_enabled(uart, true);
	gpio_set_function(tx_pin, UART_FUNCSEL_NUM(uart, tx_pin));
	gpio_set_function(rx_pin, UART_FUNCSEL_NUM(uart, rx_pin));
	// CTS/RTS are the plain UART function on both RP2040 and RP2350.
	// (UART_FUNCSEL_NUM is for TX/RX: on RP2350 it picks the auxiliary
	// function, UART TX/RX, for GPIOs 2 and 3.)
	if (cts_pin != SERIAL_NO_PIN)
		gpio_set_function((unsigned)cts_pin, GPIO_FUNC_UART);
	if (rts_pin != SERIAL_NO_PIN)
		gpio_set_function((unsigned)rts_pin, GPIO_FUNC_UART);
	uart_set_hw_flow(uart, cts_pin != SERIAL_NO_PIN, rts_pin != SERIAL_NO_PIN);

	unsigned irq = UART_IRQ_NUM(uart);
	irq_set_exclusive_handler(irq, serial_irq);
	irq_set_enabled(irq, true);
	uart_set_irqs_enabled(uart, true, false);
	// RX interrupt (and RTS watermark) at half the 32-byte FIFO: the RX
	// timeout interrupt still delivers stragglers promptly, and after RTS
	// drops there are 16 bytes of room for what the host already sent.
	hw_write_masked(&uart_get_hw(uart)->ifls, 2u << UART_UARTIFLS_RXIFLSEL_LSB,
		UART_UARTIFLS_RXIFLSEL_BITS);
}

void serial_configure(unsigned baud, unsigned data_bits, char parity, unsigned stop_bits) {
	uart_parity_t p = parity == 'E' ? UART_PARITY_EVEN : parity == 'O' ? UART_PARITY_ODD : UART_PARITY_NONE;
	uart_set_baudrate(serial_uart, baud);
	uart_set_format(serial_uart, data_bits, stop_bits, p);
}

size_t serial_read(uint8_t *buf, size_t max) {
	uint32_t t = rx_tail, available = rx_head - t;
	size_t n = available < max ? available : max;
	for (size_t i = 0; i < n; ++i)
		buf[i] = rx_buffer[(t + i) % SERIAL_RX_BUFFER_SIZE];
	// Make sure the copy is done before the IRQ may reuse the space.
	__compiler_memory_barrier();
	rx_tail = t + (uint32_t)n;
	if (rx_paused && SERIAL_RX_BUFFER_SIZE - (rx_head - rx_tail) >= RX_RESUME_ROOM) {
		rx_paused = false;
		set_rx_irqs(true); // fires at once if the FIFO has data
	}
	return n;
}

size_t serial_write(const uint8_t *data, size_t len) {
	size_t n = 0;
	for (; n < len && tx_head - tx_tail < SERIAL_TX_BUFFER_SIZE; ++n)
		tx_buffer[tx_head++ % SERIAL_TX_BUFFER_SIZE] = data[n];
	if (n < len) {
		uint32_t saved = save_and_disable_interrupts();
		stats.tx_dropped += (uint32_t)(len - n);
		restore_interrupts(saved);
	}
	serial_poll();
	return n;
}

void serial_poll(void) {
	while (tx_tail != tx_head && uart_is_writable(serial_uart))
		uart_get_hw(serial_uart)->dr = tx_buffer[tx_tail++ % SERIAL_TX_BUFFER_SIZE];
}

serial_stats_t serial_stats(void) {
	uint32_t saved = save_and_disable_interrupts();
	serial_stats_t s = stats;
	s.rx_level = rx_head - rx_tail;
	restore_interrupts(saved);
	s.tx_level = tx_head - tx_tail;
	return s;
}
