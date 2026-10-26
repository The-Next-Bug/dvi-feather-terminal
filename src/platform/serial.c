#include "serial.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "led.h"
#include "xonxoff.h"

// Receive: single producer (IRQ) and single consumer (main loop), both on
// core 0.
static uint8_t rx_buffer[SERIAL_RX_BUFFER_SIZE];
static volatile uint32_t rx_head; // written by the IRQ
static volatile uint32_t rx_tail; // written by serial_read
static volatile bool rx_paused;   // RX interrupts off: ring buffer nearly full
static volatile serial_stats_t stats;
static uart_inst_t *serial_uart;
static int cts_gpio = SERIAL_NO_PIN, rts_gpio = SERIAL_NO_PIN;
static volatile serial_flow_t flow;

// XON/XOFF: changed by the IRQ, and by the main loop with interrupts off.
static xonxoff_t xon;
static volatile uint8_t tx_control; // XON or XOFF to send ahead of the queue; 0: none

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
	if (on) {
		hw_set_bits(&hw->imsc, UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS);
	} else {
		hw_clear_bits(&hw->imsc, UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS);
	}
}

// Send tx_control if the UART has room. With interrupts off (or in the IRQ),
// so it never lands between serial_poll's room check and its write.
static void __not_in_flash_func(send_control)(void) {
	if (tx_control && uart_is_writable(serial_uart)) {
		uart_get_hw(serial_uart)->dr = tx_control;
		if (tx_control == XONXOFF_XOFF) {
			++stats.throttled;
		}
		tx_control = 0;
	}
}

// XON/XOFF: whether the receive buffer's level calls for either.
static void __not_in_flash_func(check_level)(uint32_t level) {
	uint8_t c = xonxoff_level(&xon, level);
	if (c) {
		tx_control = c;
	}
	send_control();
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
		if (dr & UART_UARTDR_OE_BITS) {
			++stats.overruns;
		}
		if (dr & (UART_UARTDR_BE_BITS | UART_UARTDR_PE_BITS | UART_UARTDR_FE_BITS)) {
			++stats.errors;
			continue;
		}
		if (flow == SERIAL_FLOW_XONXOFF && xonxoff_received(&xon, (uint8_t)dr)) {
			continue; // the host stops or restarts what we send
		}
		++stats.received;
		rx_buffer[h % SERIAL_RX_BUFFER_SIZE] = (uint8_t)dr;
		++h;
	}
	rx_head = h;
	if (flow == SERIAL_FLOW_XONXOFF) {
		check_level(h - rx_tail);
	}
}

void serial_init(uart_inst_t *uart, unsigned baud, unsigned tx_pin, unsigned rx_pin, int cts_pin,
		int rts_pin) {
	serial_uart = uart;
	cts_gpio = cts_pin;
	rts_gpio = rts_pin;
	uart_init(uart, baud);
	uart_set_format(uart, 8, 1, UART_PARITY_NONE);
	uart_set_fifo_enabled(uart, true);
	gpio_set_function(tx_pin, UART_FUNCSEL_NUM(uart, tx_pin));
	gpio_set_function(rx_pin, UART_FUNCSEL_NUM(uart, rx_pin));
	uart_set_hw_flow(uart, false, false);

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

void serial_set_flow(serial_flow_t f) {
	if (f == SERIAL_FLOW_RTSCTS && cts_gpio == SERIAL_NO_PIN && rts_gpio == SERIAL_NO_PIN) {
		f = SERIAL_FLOW_NONE;
	}
	uint32_t saved = save_and_disable_interrupts();
	// A host we held off would wait for good.
	bool release = flow == SERIAL_FLOW_XONXOFF && f != SERIAL_FLOW_XONXOFF && xon.held;
	flow = f;
	xonxoff_init(&xon, SERIAL_RX_BUFFER_SIZE);
	tx_control = release ? XONXOFF_XON : 0;
	restore_interrupts(saved);

	// CTS/RTS are the plain UART function. (UART_FUNCSEL_NUM is for TX/RX:
	// on RP2350 it picks the auxiliary function, UART TX/RX, for GPIOs 2
	// and 3.) Without RTS/CTS they have no function, as at power-on, so an
	// unwired CTS cannot hold the transmitter.
	bool hw = f == SERIAL_FLOW_RTSCTS;
	uart_set_hw_flow(serial_uart, hw && cts_gpio != SERIAL_NO_PIN, hw && rts_gpio != SERIAL_NO_PIN);
	if (cts_gpio != SERIAL_NO_PIN) {
		gpio_set_function((unsigned)cts_gpio, hw ? GPIO_FUNC_UART : GPIO_FUNC_NULL);
	}
	if (rts_gpio != SERIAL_NO_PIN) {
		gpio_set_function((unsigned)rts_gpio, hw ? GPIO_FUNC_UART : GPIO_FUNC_NULL);
	}
	serial_poll();
}

serial_flow_t serial_flow(void) {
	return flow;
}

void serial_configure(unsigned baud, unsigned data_bits, char parity, unsigned stop_bits) {
	uart_parity_t p = parity == 'E' ? UART_PARITY_EVEN : parity == 'O' ? UART_PARITY_ODD : UART_PARITY_NONE;
	uart_set_baudrate(serial_uart, baud);
	uart_set_format(serial_uart, data_bits, stop_bits, p);
}

size_t serial_read(uint8_t *buf, size_t max) {
	uint32_t t = rx_tail, available = rx_head - t;
	size_t n = available < max ? available : max;
	for (size_t i = 0; i < n; ++i) {
		buf[i] = rx_buffer[(t + i) % SERIAL_RX_BUFFER_SIZE];
	}
	// Make sure the copy is done before the IRQ may reuse the space.
	__compiler_memory_barrier();
	rx_tail = t + (uint32_t)n;
	if (n) {
		led_activity();
	}
	if (flow == SERIAL_FLOW_XONXOFF) {
		uint32_t saved = save_and_disable_interrupts();
		check_level(rx_head - rx_tail);
		restore_interrupts(saved);
	}
	if (rx_paused && SERIAL_RX_BUFFER_SIZE - (rx_head - rx_tail) >= RX_RESUME_ROOM) {
		rx_paused = false;
		set_rx_irqs(true); // fires at once if the FIFO has data
	}
	return n;
}

size_t serial_write(const uint8_t *data, size_t len) {
	size_t n = 0;
	for (; n < len && tx_head - tx_tail < SERIAL_TX_BUFFER_SIZE; ++n) {
		tx_buffer[tx_head++ % SERIAL_TX_BUFFER_SIZE] = data[n];
	}
	if (n) {
		led_activity();
	}
	if (n < len) {
		uint32_t saved = save_and_disable_interrupts();
		stats.tx_dropped += (uint32_t)(len - n);
		restore_interrupts(saved);
	}
	serial_poll();
	return n;
}

void serial_poll(void) {
	// Interrupts off: the IRQ may send XON/XOFF into the same FIFO. At most
	// 32 register writes.
	uint32_t saved = save_and_disable_interrupts();
	send_control();
	if (!tx_control && !(flow == SERIAL_FLOW_XONXOFF && xon.stopped)) {
		while (tx_tail != tx_head && uart_is_writable(serial_uart)) {
			uart_get_hw(serial_uart)->dr = tx_buffer[tx_tail++ % SERIAL_TX_BUFFER_SIZE];
		}
	}
	restore_interrupts(saved);
}

serial_stats_t serial_stats(void) {
	uint32_t saved = save_and_disable_interrupts();
	serial_stats_t s = stats;
	s.rx_level = rx_head - rx_tail;
	restore_interrupts(saved);
	s.tx_level = tx_head - tx_tail;
	return s;
}
