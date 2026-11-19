#include "carrier.h"

void carrier_init(carrier_t *c, bool dcd, uint32_t now_ms) {
	*c = (carrier_t){.up = dcd, .raw = dcd, .raw_since = now_ms, .dtr = true};
}

void carrier_hangup(carrier_t *c, uint32_t now_ms) {
	if (c->hanging_up) {
		return;
	}
	c->hanging_up = true;
	c->dropped = !c->up;
	c->hangup_since = now_ms;
	c->dtr = false;
}

carrier_event_t carrier_poll(carrier_t *c, bool dcd, uint32_t now_ms) {
	if (dcd != c->raw) {
		c->raw = dcd;
		c->raw_since = now_ms;
	}
	carrier_event_t event = CARRIER_NONE;
	if (c->raw != c->up && now_ms - c->raw_since >= CARRIER_DEBOUNCE_MS) {
		c->up = c->raw;
		event = c->up ? CARRIER_UP : CARRIER_LOST;
		if (!c->up && c->hanging_up) {
			c->dropped = true;
		}
	}
	if (event != CARRIER_NONE || !c->hanging_up) {
		return event; // LOST first; HUNG_UP at a later poll
	}
	uint32_t held = now_ms - c->hangup_since;
	if (c->dropped && held >= CARRIER_DTR_MS) {
		c->hanging_up = false;
		c->dtr = true;
		return CARRIER_HUNG_UP;
	}
	if (!c->dropped && held >= CARRIER_HANGUP_MS) {
		c->hanging_up = false;
		c->dtr = true;
		return CARRIER_STUCK;
	}
	return CARRIER_NONE;
}
