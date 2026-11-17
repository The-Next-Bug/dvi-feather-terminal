#include "telnet.h"

#include <string.h>

enum {
	TN_DATA,
	TN_PROBE,  // TELNET_AUTO before a command: an IAC, maybe data
	TN_IAC,    // after IAC
	TN_OPT,    // after IAC WILL/WONT/DO/DONT
	TN_SB_OPT, // after IAC SB
	TN_SB,     // inside a sub-negotiation
	TN_SB_IAC, // an IAC inside it
};

static bool is_verb(uint8_t c) {
	return c >= TELNET_WILL && c <= TELNET_DONT;
}

static uint32_t bit(uint8_t opt) {
	return opt < 32 ? 1u << opt : 0;
}

void telnet_init(telnet_t *t, telnet_send_t send, void *ctx) {
	*t = (telnet_t){.mode = TELNET_OFF, .cols = 80, .rows = 24, .send = send, .ctx = ctx};
	telnet_set_type(t, "VT100");
}

void telnet_reset(telnet_t *t) {
	t->seen = t->naws = false;
	t->will = t->dos = 0;
	t->state = TN_DATA;
}

void telnet_set_mode(telnet_t *t, telnet_mode_t mode) {
	if (mode != t->mode) {
		t->mode = mode;
		telnet_reset(t);
	}
}

bool telnet_active(const telnet_t *t) {
	return t->mode == TELNET_ON || (t->mode == TELNET_AUTO && t->seen);
}

void telnet_set_type(telnet_t *t, const char *name) {
	size_t n = 0;
	for (; *name && n < TELNET_TYPE_MAX; ++name) {
		if (*name > 0x20 && *name < 0x7f) {
			t->type[n++] = *name;
		}
	}
	t->type[n] = '\0';
}

static void send3(telnet_t *t, uint8_t verb, uint8_t opt) {
	const uint8_t b[3] = {TELNET_IAC, verb, opt};
	t->send(t->ctx, b, sizeof b);
}

// IAC SB NAWS width height IAC SE, 16 bits each, any 255 doubled.
static void send_naws(telnet_t *t) {
	uint8_t b[16];
	size_t n = 0;
	b[n++] = TELNET_IAC;
	b[n++] = TELNET_SB;
	b[n++] = TELNET_OPT_NAWS;
	const uint8_t v[4] = {(uint8_t)(t->cols >> 8), (uint8_t)t->cols, (uint8_t)(t->rows >> 8), (uint8_t)t->rows};
	for (unsigned i = 0; i < 4; ++i) {
		b[n++] = v[i];
		if (v[i] == TELNET_IAC) {
			b[n++] = TELNET_IAC;
		}
	}
	b[n++] = TELNET_IAC;
	b[n++] = TELNET_SE;
	t->send(t->ctx, b, n);
}

void telnet_set_size(telnet_t *t, unsigned cols, unsigned rows) {
	if (cols == t->cols && rows == t->rows) {
		return;
	}
	t->cols = cols;
	t->rows = rows;
	if (t->naws && telnet_active(t)) {
		send_naws(t);
	}
}

static void negotiate(telnet_t *t, uint8_t verb, uint8_t opt) {
	switch (verb) {
	case TELNET_DO:
		if (opt == TELNET_OPT_NAWS || opt == TELNET_OPT_TTYPE || opt == TELNET_OPT_SGA) {
			t->will |= bit(opt);
			send3(t, TELNET_WILL, opt);
			if (opt == TELNET_OPT_NAWS) {
				t->naws = true;
				send_naws(t);
			}
		} else {
			send3(t, TELNET_WONT, opt);
		}
		break;
	case TELNET_DONT:
		if (opt == TELNET_OPT_NAWS) {
			t->naws = false;
		}
		if (t->will & bit(opt)) { // only a change is answered
			t->will &= ~bit(opt);
			send3(t, TELNET_WONT, opt);
		}
		break;
	case TELNET_WILL:
		if (opt == TELNET_OPT_ECHO || opt == TELNET_OPT_SGA) {
			t->dos |= bit(opt);
			send3(t, TELNET_DO, opt);
		} else {
			send3(t, TELNET_DONT, opt);
		}
		break;
	case TELNET_WONT:
		if (t->dos & bit(opt)) {
			t->dos &= ~bit(opt);
			send3(t, TELNET_DONT, opt);
		}
		break;
	default:
		break;
	}
}

// A sub-negotiation's end: TTYPE SEND gets IAC SB TTYPE IS name IAC SE.
static void subnegotiation(telnet_t *t) {
	if (t->sb_opt != TELNET_OPT_TTYPE || t->sb_len < 1 || t->sb[0] != TELNET_TTYPE_SEND) {
		return;
	}
	uint8_t b[TELNET_TYPE_MAX + 6];
	size_t n = 0;
	b[n++] = TELNET_IAC;
	b[n++] = TELNET_SB;
	b[n++] = TELNET_OPT_TTYPE;
	b[n++] = TELNET_TTYPE_IS;
	size_t len = strlen(t->type);
	memcpy(b + n, t->type, len); // printable ASCII: never an IAC
	n += len;
	b[n++] = TELNET_IAC;
	b[n++] = TELNET_SE;
	t->send(t->ctx, b, n);
}

size_t telnet_filter(telnet_t *t, const uint8_t *in, size_t n, uint8_t *out) {
	if (t->mode == TELNET_OFF) {
		memcpy(out, in, n);
		return n;
	}
	size_t o = 0;
	for (size_t i = 0; i < n; ++i) {
		uint8_t c = in[i];
		switch (t->state) {
		case TN_DATA:
			if (c == TELNET_IAC) {
				t->state = telnet_active(t) ? TN_IAC : TN_PROBE;
			} else {
				out[o++] = c;
			}
			break;
		case TN_PROBE:
			if (is_verb(c) || c == TELNET_SB) {
				t->seen = true; // telnet from here on
				t->state = c == TELNET_SB ? TN_SB_OPT : TN_OPT;
				t->verb = c;
			} else {
				out[o++] = TELNET_IAC; // a plain host's 0xFF
				if (c == TELNET_IAC) {
					t->state = TN_PROBE; // this one may start a command
				} else {
					out[o++] = c;
					t->state = TN_DATA;
				}
			}
			break;
		case TN_IAC:
			if (c == TELNET_IAC) {
				out[o++] = TELNET_IAC; // data 0xFF
				t->state = TN_DATA;
			} else if (is_verb(c)) {
				t->seen = true;
				t->verb = c;
				t->state = TN_OPT;
			} else if (c == TELNET_SB) {
				t->seen = true;
				t->state = TN_SB_OPT;
			} else {
				t->state = TN_DATA; // NOP, GA, AYT, a stray SE ...: nothing to do
			}
			break;
		case TN_OPT:
			negotiate(t, t->verb, c);
			t->state = TN_DATA;
			break;
		case TN_SB_OPT:
			t->sb_opt = c;
			t->sb_len = 0;
			t->state = TN_SB;
			break;
		case TN_SB:
			if (c == TELNET_IAC) {
				t->state = TN_SB_IAC;
			} else if (t->sb_len < sizeof t->sb) {
				t->sb[t->sb_len++] = c;
			}
			break;
		case TN_SB_IAC:
			if (c == TELNET_SE) {
				subnegotiation(t);
				t->state = TN_DATA;
			} else {
				if (c == TELNET_IAC && t->sb_len < sizeof t->sb) {
					t->sb[t->sb_len++] = c; // IAC IAC: a 255 in it
				}
				t->state = TN_SB;
			}
			break;
		default:
			t->state = TN_DATA;
			break;
		}
	}
	return o;
}

void telnet_send_data(telnet_t *t, const uint8_t *data, size_t len) {
	if (!telnet_active(t)) {
		t->send(t->ctx, data, len);
		return;
	}
	size_t start = 0;
	for (size_t i = 0; i < len; ++i) {
		if (data[i] == TELNET_IAC) {
			t->send(t->ctx, data + start, i + 1 - start); // up to and with the IAC
			start = i; // and the IAC again
		}
	}
	if (start < len) {
		t->send(t->ctx, data + start, len - start);
	}
}
