#include "lz.h"

#define MASK (LZ_WINDOW - 1)
#define HALF (LZ_WINDOW / 2)

typedef struct {
	uint8_t *window;
	size_t pos, flushed, out_size;
	lz_sink_t sink;
	void *ctx;
} out_t;

// Hand the sink what is written but not yet passed on: a half window at a
// time, so each chunk is contiguous in the window and nothing is
// overwritten before it has been passed on.
static bool flush(out_t *o) {
	while (o->flushed < o->pos) {
		size_t end = (o->flushed / HALF + 1) * HALF;
		if (end > o->pos) {
			end = o->pos;
		}
		if (!o->sink(o->ctx, o->window + (o->flushed & MASK), end - o->flushed)) {
			return false;
		}
		o->flushed = end;
	}
	return true;
}

static bool put(out_t *o, uint8_t b) {
	if (o->pos == o->out_size) {
		return false;
	}
	o->window[o->pos & MASK] = b;
	++o->pos;
	return o->pos % HALF != 0 || flush(o);
}

// A length continued in extra bytes (after a nibble of 15).
static bool more_length(const uint8_t **p, const uint8_t *end, size_t *n) {
	uint8_t b;
	do {
		if (*p == end) {
			return false;
		}
		b = *(*p)++;
		*n += b;
	} while (b == 255);
	return true;
}

bool lz_decode(const uint8_t *src, size_t len, size_t out_size, uint8_t *window, lz_sink_t sink, void *ctx) {
	out_t o = {.window = window, .out_size = out_size, .sink = sink, .ctx = ctx};
	const uint8_t *p = src, *end = src + len;
	while (p < end) {
		uint8_t token = *p++;
		size_t literals = token >> 4;
		if (literals == 15 && !more_length(&p, end, &literals)) {
			return false;
		}
		if (literals > (size_t)(end - p)) {
			return false;
		}
		while (literals--) {
			if (!put(&o, *p++)) {
				return false;
			}
		}
		if (p == end) {
			break; // the last sequence: literals only
		}
		if (end - p < 2) {
			return false;
		}
		size_t offset = (size_t)p[0] | (size_t)p[1] << 8;
		p += 2;
		size_t match = token & 15;
		if (match == 15 && !more_length(&p, end, &match)) {
			return false;
		}
		match += 4;
		if (offset == 0 || offset > LZ_MAX_OFFSET || offset > o.pos) {
			return false;
		}
		while (match--) {
			if (!put(&o, o.window[(o.pos - offset) & MASK])) {
				return false;
			}
		}
	}
	return o.pos == out_size && flush(&o);
}
