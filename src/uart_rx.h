#ifndef UART_RX_H
#define UART_RX_H

// Interrupt-driven UART receive into a ring buffer, drained by the main loop
// on core 0. TX is set up but unused until there is a keyboard.

#include <stddef.h>
#include <stdint.h>

#include "hardware/uart.h"

#define UART_RX_BUFFER_SIZE 4096 // power of two

typedef struct {
	uint32_t received;  // bytes received
	uint32_t overruns;  // hardware FIFO overruns (bytes lost before the IRQ ran)
	uint32_t dropped;   // bytes lost because the ring buffer was full
	uint32_t errors;    // framing, parity or break errors
} uart_rx_stats_t;

// Call after the system clock is final (the baud divisor depends on it).
void uart_rx_init(uart_inst_t *uart, unsigned baud, unsigned tx_pin, unsigned rx_pin);

// Copy up to max buffered bytes into buf; returns the number copied.
size_t uart_rx_read(uint8_t *buf, size_t max);

uart_rx_stats_t uart_rx_stats(void);

#endif
