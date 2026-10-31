#ifndef SERIAL_H
#define SERIAL_H

// The host serial line on a UART (UART0 here), interrupt-driven
// on core 0.
//
// Receive: bytes go into a ring buffer, drained by the main loop. With
// RTS/CTS, the UART asserts RTS by itself while its RX FIFO is below the
// watermark; when the ring buffer is nearly full the interrupt stops draining
// the FIFO, so the FIFO fills, RTS drops and the host pauses. Reading makes
// room and resumes it. Nothing is lost as long as the host honors RTS.
// With XON/XOFF (xonxoff.h), XOFF goes out once the ring is half full and
// XON once it has drained, ahead of anything queued; the FIFO is still
// drained after XOFF, for what the host sends before it stops.
//
// Transmit: replies and keystrokes are queued and moved to the UART by
// serial_poll as it has room, so a host holding CTS off (or an unwired CTS
// pin pulled high), or one that sent XOFF, delays them instead of hanging
// the terminal.

#include <stddef.h>
#include <stdint.h>

#include "hardware/uart.h"

#define SERIAL_RX_BUFFER_SIZE 2048 // power of two: ~180 ms at 115200 baud
#define SERIAL_TX_BUFFER_SIZE 256  // power of two
#define SERIAL_NO_PIN (-1)

typedef enum {
	SERIAL_FLOW_NONE,
	SERIAL_FLOW_RTSCTS, // the UART's hardware flow control on the CTS/RTS pins
	SERIAL_FLOW_XONXOFF, // XON/XOFF in both directions, taken out of the received bytes
} serial_flow_t;

typedef struct {
	uint32_t received;  // bytes received
	uint32_t overruns;  // hardware FIFO overruns (bytes lost before the IRQ ran)
	uint32_t dropped;   // bytes lost because the ring buffer was full
	uint32_t errors;    // framing, parity or break errors
	uint32_t throttled; // times the receive side held the host off (RTS, or XOFF sent)
	uint32_t tx_dropped; // queued bytes lost because the TX queue was full
	uint32_t rx_level;  // bytes waiting in the receive buffer (of SERIAL_RX_BUFFER_SIZE)
	uint32_t tx_level;  // bytes waiting in the transmit queue (of SERIAL_TX_BUFFER_SIZE)
} serial_stats_t;

// Call after the system clock is final (the baud divisor depends on it).
// cts_pin/rts_pin are the pins SERIAL_FLOW_RTSCTS uses (SERIAL_NO_PIN:
// none); it starts with no flow control.
void serial_init(uart_inst_t *uart, unsigned baud, unsigned tx_pin, unsigned rx_pin, int cts_pin,
	int rts_pin);

// Change the flow control. RTS/CTS without either pin is none; RTS/CTS
// left puts the pins back to no function (unwired, as at power-on);
// XON/XOFF left sends XON if the host was held off by XOFF.
void serial_set_flow(serial_flow_t flow);

serial_flow_t serial_flow(void);

// Change the line settings: baud rate, data bits (5-8), parity ('N', 'E' or
// 'O') and stop bits (1 or 2). serial_init starts at 8N1.
void serial_configure(unsigned baud, unsigned data_bits, char parity, unsigned stop_bits);
// The baud rate in use (serial_init's or serial_configure's).
unsigned serial_baud(void);
// Whether the UART can run at baud (not below ~120 at clk_peri 128 MHz).
bool serial_baud_possible(unsigned baud);

// Copy up to max received bytes into buf; returns the number copied.
size_t serial_read(uint8_t *buf, size_t max);

// Queue bytes to send; returns how many fit (the rest are counted as
// tx_dropped).
size_t serial_write(const uint8_t *data, size_t len);

// How many bytes serial_write would take now. Moves queued bytes to the
// UART first, so a caller waiting for room sees it free up.
size_t serial_tx_room(void);

// Move queued bytes to the UART while it has room. Call from the main loop.
void serial_poll(void);

serial_stats_t serial_stats(void);

#endif
