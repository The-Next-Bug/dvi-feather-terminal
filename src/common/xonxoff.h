#ifndef XONXOFF_H
#define XONXOFF_H

// XON/XOFF software flow control (the Flow control setting's XON/XOFF;
// bd 68t.1): the decisions, without the UART. Portable C (no pico-sdk),
// host-tested; serial.c calls it from its receive interrupt and the main
// loop, with interrupts off around each call.
//
// Receive: once the receive buffer holds XONXOFF stop level bytes, ask for
// XOFF; if bytes keep coming (a host's driver and USB adapter send a few
// hundred more), XOFF again every further step bytes; XON when it has
// drained to the start level. The stop level is half the buffer, so what
// arrives after XOFF still fits.
//
// Transmit: XOFF and XON from the host are taken out of the received
// bytes and stop or restart what the terminal sends.

#include <stdbool.h>
#include <stdint.h>

#define XONXOFF_XON 0x11  // DC1, ^Q
#define XONXOFF_XOFF 0x13 // DC3, ^S

typedef struct {
	uint32_t stop, start, step; // levels in bytes waiting (xonxoff_init)
	uint32_t resend;            // XOFF again at this level
	bool held;                  // we sent XOFF and no XON since
	bool stopped;               // the host sent XOFF and no XON since
} xonxoff_t;

// For a receive buffer of size bytes; nothing held or stopped.
void xonxoff_init(xonxoff_t *x, uint32_t size);

// A received byte: true if it was XON or XOFF (not data: drop it).
bool xonxoff_received(xonxoff_t *x, uint8_t c);

// With level bytes waiting in the receive buffer: XONXOFF_XOFF or
// XONXOFF_XON to send ahead of anything queued, or 0 for nothing.
uint8_t xonxoff_level(xonxoff_t *x, uint32_t level);

#endif
