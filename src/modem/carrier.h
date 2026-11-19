#ifndef CARRIER_H
#define CARRIER_H

// The WiFi modem's DCD and DTR for calls (bd xir.17.5): Zimodem holds DCD
// active while it has a connection open (any: a dialed stream, a print
// job), and with S63=2 drops the call in stream mode when DTR goes
// inactive (modem.c sets both up). Portable C, host-tested (test_modem);
// apps/terminal/telnet_glue.c drives it with the pins (modem_lines.c).
//
// The carrier: up once DCD has been active CARRIER_DEBOUNCE_MS, lost once
// inactive that long (a glitch on the wire is no hang-up). A hang-up: DTR
// inactive at least CARRIER_DTR_MS, until DCD is inactive (the modem has
// dropped the call), then active again; if DCD stays active
// CARRIER_HANGUP_MS (the modem not in stream mode, S63 not 2, a wire off),
// DTR comes back and the caller falls back to +++ and ATH.

#include <stdbool.h>
#include <stdint.h>

#define CARRIER_DEBOUNCE_MS 50
#define CARRIER_DTR_MS 100
#define CARRIER_HANGUP_MS 2000

typedef enum {
	CARRIER_NONE,    // nothing new
	CARRIER_UP,      // a connection opened
	CARRIER_LOST,    // it closed (the far end, or a hang-up's end)
	CARRIER_HUNG_UP, // the hang-up worked: DCD dropped with DTR (after CARRIER_LOST)
	CARRIER_STUCK,   // the hang-up did not drop the call: fall back to +++ / ATH
} carrier_event_t;

typedef struct {
	bool up;          // the carrier, debounced
	bool raw;         // DCD as last seen
	uint32_t raw_since;
	bool hanging_up;  // DTR inactive for a hang-up
	bool dropped;     // ... and the carrier has gone
	uint32_t hangup_since;
	bool dtr;         // DTR as wanted: true = active
} carrier_t;

void carrier_init(carrier_t *c, bool dcd, uint32_t now_ms);

// Hang up the call (no-op while one is under way, or without a carrier:
// then CARRIER_HUNG_UP at the next poll, nothing to drop).
void carrier_hangup(carrier_t *c, uint32_t now_ms);

// DCD now (true = active); returns what happened. Afterwards c->dtr is
// what DTR should be.
carrier_event_t carrier_poll(carrier_t *c, bool dcd, uint32_t now_ms);

#endif
