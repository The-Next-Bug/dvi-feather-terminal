#ifndef SERIAL_H
#define SERIAL_H

// The host serial line on a UART (UART0 on the Feather), interrupt-driven
// on core 0.
//
// Receive: bytes go into a ring buffer, drained by the main loop. With
// RTS/CTS, the UART asserts RTS by itself while its RX FIFO is below the
// watermark; when the ring buffer is nearly full the interrupt stops draining
// the FIFO, so the FIFO fills, RTS drops and the host pauses. Reading makes
// room and resumes it. Nothing is lost as long as the host honours RTS.
//
// Transmit: replies (and later keystrokes) are queued and moved to the UART
// by serial_poll as it has room, so a host holding CTS off (or an unwired
// CTS pin pulled high) delays them instead of hanging the terminal.

#include <stddef.h>
#include <stdint.h>

#include "hardware/uart.h"

#define SERIAL_RX_BUFFER_SIZE 4096 // power of two
#define SERIAL_TX_BUFFER_SIZE 256  // power of two
#define SERIAL_NO_PIN (-1)

typedef struct {
	uint32_t received;  // bytes received
	uint32_t overruns;  // hardware FIFO overruns (bytes lost before the IRQ ran)
	uint32_t dropped;   // bytes lost because the ring buffer was full
	uint32_t errors;    // framing, parity or break errors
	uint32_t throttled; // times the receive side held the host off (RTS)
	uint32_t tx_dropped; // queued bytes lost because the TX queue was full
} serial_stats_t;

// Call after the system clock is final (the baud divisor depends on it).
// cts_pin/rts_pin enable hardware flow control; pass SERIAL_NO_PIN for none.
void serial_init(uart_inst_t *uart, unsigned baud, unsigned tx_pin, unsigned rx_pin, int cts_pin,
	int rts_pin);

// Copy up to max received bytes into buf; returns the number copied.
size_t serial_read(uint8_t *buf, size_t max);

// Queue bytes to send; returns how many fit (the rest are counted as
// tx_dropped).
size_t serial_write(const uint8_t *data, size_t len);

// Move queued bytes to the UART while it has room. Call from the main loop.
void serial_poll(void);

serial_stats_t serial_stats(void);

#endif
