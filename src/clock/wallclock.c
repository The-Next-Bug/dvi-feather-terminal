#include "wallclock.h"

void wallclock_start(wallclock_t *w, uint32_t secs, uint64_t now_us, bool on_second) {
	w->base_secs = secs;
	w->base_us = now_us;
	w->on_second = on_second;
}

uint32_t wallclock_now(const wallclock_t *w, uint64_t now_us) {
	return w->base_secs + (uint32_t)((now_us - w->base_us) / WALLCLOCK_TICK_US);
}

uint32_t wallclock_fraction(const wallclock_t *w, uint64_t now_us) {
	return (uint32_t)((now_us - w->base_us) % WALLCLOCK_TICK_US);
}

void wallclock_edge(wallclock_t *w, uint64_t edge_us) {
	uint64_t elapsed = edge_us - w->base_us;
	uint32_t n;
	if (w->on_second) {
		// Seconds since the last second's start, to the nearest: a missed
		// edge (interrupts off a while) counts.
		n = (uint32_t)((elapsed + WALLCLOCK_TICK_US / 2) / WALLCLOCK_TICK_US);
		if (n == 0) {
			return;
		}
	} else {
		// The base was read somewhere within its second: the next start
		// is under a second after it.
		n = (uint32_t)(elapsed / WALLCLOCK_TICK_US) + 1;
	}
	w->base_secs += n;
	w->base_us = edge_us;
	w->on_second = true;
	w->edges = true;
	w->last_edge_us = edge_us;
}

bool wallclock_ticking(const wallclock_t *w, uint64_t now_us) {
	return w->edges && now_us - w->last_edge_us < WALLCLOCK_TICK_TIMEOUT_US;
}
