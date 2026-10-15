#include "blank.h"

void blank_init(blank_t *b, uint32_t after_ms, uint32_t now_ms) {
	*b = (blank_t){.after_ms = after_ms, .last_ms = now_ms};
}

bool blank_activity(blank_t *b, uint32_t now_ms) {
	bool woke = b->blanked;
	b->last_ms = now_ms;
	b->blanked = false;
	return woke;
}

bool blank_tick(blank_t *b, uint32_t now_ms) {
	// Compared by difference, so the millisecond count may wrap.
	if (b->after_ms && now_ms - b->last_ms >= b->after_ms) {
		b->blanked = true;
	}
	return b->blanked;
}
