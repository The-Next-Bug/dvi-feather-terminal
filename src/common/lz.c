#include "lz.h"

#include <string.h>

// Raw deflate (RFC 1951) in the tinf manner: canonical Huffman codes kept
// as a count per length plus the symbols in code order, decoded a bit at a
// time. Slow next to a table lookup, but small, and the card's writes are
// the install's real cost.

#define MASK (LZ_DICT - 1)
#define HALF (LZ_DICT / 2)

typedef struct {
	uint16_t counts[16];   // codes of each length (0 unused)
	uint16_t symbols[288]; // the symbols, by code
} tree_t;

typedef struct {
	tree_t lit, dist;        // the block's literal/length and distance codes
	uint8_t lengths[288 + 32]; // code lengths while a dynamic block's header is read
} tables_t;

// The work area: the dictionary, then the tables.
_Static_assert(LZ_DICT + sizeof(tables_t) <= LZ_WORK, "the tables fit beside the dictionary");

typedef struct {
	const uint8_t *p, *end;
	uint32_t bits;  // read but not used, low bit first
	unsigned count; // how many
	bool overrun;   // read past the end (zeros were returned)
	uint8_t *window;
	size_t pos, flushed, out_size;
	lz_sink_t sink;
	void *ctx;
} state_t;

// Hand the sink what is written but not yet passed on: a half dictionary
// at a time, so each chunk is contiguous and nothing is overwritten before
// it has been passed on.
static bool flush(state_t *s) {
	while (s->flushed < s->pos) {
		size_t end = (s->flushed / HALF + 1) * HALF;
		if (end > s->pos) {
			end = s->pos;
		}
		if (!s->sink(s->ctx, s->window + (s->flushed & MASK), end - s->flushed)) {
			return false;
		}
		s->flushed = end;
	}
	return true;
}

static bool put(state_t *s, uint8_t b) {
	if (s->pos == s->out_size) {
		return false;
	}
	s->window[s->pos & MASK] = b;
	++s->pos;
	return s->pos % HALF != 0 || flush(s);
}

static unsigned bits(state_t *s, unsigned n) {
	while (s->count < n) {
		uint32_t b = 0;
		if (s->p < s->end) {
			b = *s->p++;
		} else {
			s->overrun = true;
		}
		s->bits |= b << s->count;
		s->count += 8;
	}
	unsigned v = s->bits & ((1u << n) - 1);
	s->bits >>= n;
	s->count -= n;
	return v;
}

// lengths[0 .. n) -> t; false if over-subscribed (an incomplete set, as a
// single distance code, is allowed).
static bool build(tree_t *t, const uint8_t *lengths, unsigned n) {
	uint16_t offs[16];
	memset(t->counts, 0, sizeof t->counts);
	for (unsigned i = 0; i < n; ++i) {
		++t->counts[lengths[i]];
	}
	t->counts[0] = 0;
	int left = 1;
	for (unsigned i = 1; i < 16; ++i) {
		left = 2 * left - t->counts[i];
		if (left < 0) {
			return false;
		}
	}
	unsigned sum = 0;
	for (unsigned i = 0; i < 16; ++i) {
		offs[i] = (uint16_t)sum;
		sum += t->counts[i];
	}
	for (unsigned i = 0; i < n; ++i) {
		if (lengths[i]) {
			t->symbols[offs[lengths[i]]++] = (uint16_t)i;
		}
	}
	return true;
}

// The next symbol, or -1 for a code the tree does not have.
static int symbol(state_t *s, const tree_t *t) {
	int sum = 0, cur = 0;
	for (unsigned len = 1; len < 16; ++len) {
		cur = 2 * cur + (int)bits(s, 1);
		sum += t->counts[len];
		cur -= t->counts[len];
		if (cur < 0) {
			return t->symbols[sum + cur];
		}
	}
	return -1;
}

static void fixed_trees(tables_t *tb) {
	uint8_t *l = tb->lengths;
	memset(l, 8, 144);
	memset(l + 144, 9, 112);
	memset(l + 256, 7, 24);
	memset(l + 280, 8, 8);
	build(&tb->lit, l, 288);
	memset(l, 5, 30);
	build(&tb->dist, l, 30);
}

static bool dynamic_trees(state_t *s, tables_t *tb) {
	static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	unsigned hlit = bits(s, 5) + 257, hdist = bits(s, 5) + 1, hclen = bits(s, 4) + 4;
	if (hlit > 286 || hdist > 30) {
		return false;
	}
	uint8_t *l = tb->lengths;
	memset(l, 0, 19);
	for (unsigned i = 0; i < hclen; ++i) {
		l[order[i]] = (uint8_t)bits(s, 3);
	}
	if (!build(&tb->lit, l, 19)) { // the code lengths' code, for now
		return false;
	}
	for (unsigned n = 0; n < hlit + hdist;) {
		int sym = symbol(s, &tb->lit);
		unsigned repeat;
		uint8_t value = 0;
		if (sym < 0 || s->overrun) {
			return false;
		}
		if (sym < 16) {
			l[n++] = (uint8_t)sym;
			continue;
		}
		if (sym == 16) {
			if (n == 0) {
				return false;
			}
			value = l[n - 1];
			repeat = 3 + bits(s, 2);
		} else if (sym == 17) {
			repeat = 3 + bits(s, 3);
		} else {
			repeat = 11 + bits(s, 7);
		}
		if (repeat > hlit + hdist - n) {
			return false;
		}
		memset(l + n, value, repeat);
		n += repeat;
	}
	if (l[256] == 0) {
		return false; // no end-of-block code
	}
	return build(&tb->lit, l, hlit) && build(&tb->dist, l + hlit, hdist);
}

static bool block(state_t *s, const tables_t *tb) {
	static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
		59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
		4, 5, 5, 5, 5, 0};
	static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
		513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
	static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
		10, 10, 11, 11, 12, 12, 13, 13};
	for (;;) {
		int sym = symbol(s, &tb->lit);
		if (sym < 0 || s->overrun) {
			return false;
		}
		if (sym < 256) {
			if (!put(s, (uint8_t)sym)) {
				return false;
			}
			continue;
		}
		if (sym == 256) {
			return true;
		}
		sym -= 257;
		if (sym >= 29) {
			return false;
		}
		size_t len = len_base[sym] + bits(s, len_extra[sym]);
		int d = symbol(s, &tb->dist);
		if (d < 0 || d >= 30) {
			return false;
		}
		size_t dist = dist_base[d] + bits(s, dist_extra[d]);
		if (s->overrun || dist > LZ_DICT || dist > s->pos) {
			return false;
		}
		while (len--) {
			if (!put(s, s->window[(s->pos - dist) & MASK])) {
				return false;
			}
		}
	}
}

static bool stored(state_t *s) {
	s->bits = 0; // to the byte boundary (count is a multiple of 8 then)
	s->count = 0;
	if (s->end - s->p < 4) {
		return false;
	}
	unsigned len = s->p[0] | s->p[1] << 8, nlen = s->p[2] | s->p[3] << 8;
	s->p += 4;
	if ((len ^ 0xffff) != nlen || len > (size_t)(s->end - s->p)) {
		return false;
	}
	while (len--) {
		if (!put(s, *s->p++)) {
			return false;
		}
	}
	return true;
}

bool lz_decode(const uint8_t *src, size_t len, size_t out_size, uint8_t *work, lz_sink_t sink, void *ctx) {
	state_t s = {.p = src, .end = src + len, .window = work, .out_size = out_size, .sink = sink, .ctx = ctx};
	tables_t *tb = (tables_t *)(void *)(work + LZ_DICT);
	bool last;
	do {
		last = bits(&s, 1);
		unsigned type = bits(&s, 2);
		bool ok;
		if (type == 0) {
			ok = stored(&s);
		} else if (type == 1) {
			fixed_trees(tb);
			ok = block(&s, tb);
		} else if (type == 2) {
			ok = dynamic_trees(&s, tb) && block(&s, tb);
		} else {
			ok = false;
		}
		if (!ok || s.overrun) {
			return false;
		}
	} while (!last);
	return s.pos == out_size && flush(&s);
}
