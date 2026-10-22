#ifndef WALLCLOCK_H
#define WALLCLOCK_H

// The wall clock between reads of the RTC chip: a second (datetime.h's
// seconds since 31.12.1977) at a microsecond timestamp of a monotonic
// timer, from which it counts on. The chip's 1 Hz output, when there is
// one, gives each second's start (wallclock_edge): the clock then keeps
// the chip's time instead of drifting with the timer, and its seconds turn
// over with the chip's. Portable; the caller keeps the timer and any
// interrupt locking.

#include <stdbool.h>
#include <stdint.h>

#define WALLCLOCK_TICK_US 1000000u
// Edges further apart than this: the 1 Hz output is not ticking.
#define WALLCLOCK_TICK_TIMEOUT_US 1500000u

typedef struct {
	uint32_t base_secs; // the time at base_us
	uint64_t base_us;
	bool on_second;     // base_us is a second's start (an edge, or a set)
	bool edges;         // last_edge_us is valid
	uint64_t last_edge_us;
} wallclock_t;

// secs at now_us; on_second: now_us is that second's start (the chip was
// just written), not some time within it (it was read).
void wallclock_start(wallclock_t *w, uint32_t secs, uint64_t now_us, bool on_second);
uint32_t wallclock_now(const wallclock_t *w, uint64_t now_us);
// The microseconds since the current second started (by the timer between
// edges).
uint32_t wallclock_fraction(const wallclock_t *w, uint64_t now_us);
// The chip's seconds went up at edge_us: as many seconds as have passed
// since the base, at least one if the base was within a second, none for
// an edge under half a second after the last second's start (a glitch).
// The time never goes back.
void wallclock_edge(wallclock_t *w, uint64_t edge_us);
// An edge within WALLCLOCK_TICK_TIMEOUT_US.
bool wallclock_ticking(const wallclock_t *w, uint64_t now_us);

#endif
