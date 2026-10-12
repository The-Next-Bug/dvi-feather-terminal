#include "vtparse.h"

#include <string.h>

// States from Williams' diagram. The five DCS states and SOS/PM/APC collapse
// into STRING, since their contents are discarded; OSC is separate only
// because BEL also ends it (as in xterm).
enum {
	GROUND,
	ESCAPE,
	ESCAPE_INTERMEDIATE,
	CSI_ENTRY,
	CSI_PARAM,
	CSI_INTERMEDIATE,
	CSI_IGNORE,
	STRING,
	OSC_STRING,
};

#define CAN 0x18
#define SUB 0x1a
#define ESC 0x1b
#define BEL 0x07
#define DEL 0x7f

void vtparse_init(vtparse_t *p, vtparse_callback_t callback, void *user) {
	memset(p, 0, sizeof *p);
	p->callback = callback;
	p->user = user;
	p->state = GROUND;
}

static void clear(vtparse_t *p) {
	p->private_marker = 0;
	p->n_intermediates = 0;
	p->n_params = 0;
	memset(p->params, 0, sizeof p->params);
	p->ignoring = false;
	p->params_full = false;
	p->string_len = 0;
}

void vtparse_reset(vtparse_t *p) {
	clear(p);
	p->state = GROUND;
}

static void collect(vtparse_t *p, uint8_t ch) {
	if (p->n_intermediates < VTPARSE_MAX_INTERMEDIATES)
		p->intermediates[p->n_intermediates++] = ch;
	else
		p->ignoring = true;
}

static void param(vtparse_t *p, uint8_t ch) {
	if (p->n_params == 0)
		p->n_params = 1;
	if (ch == ';') {
		if (p->n_params < VTPARSE_MAX_PARAMS)
			++p->n_params;
		else
			p->params_full = true; // as Williams: extra parameters are dropped
		return;
	}
	if (p->params_full)
		return;
	uint16_t *v = &p->params[p->n_params - 1];
	unsigned next = *v * 10u + (unsigned)(ch - '0');
	*v = next > 0xffff ? 0xffff : (uint16_t)next;
}

static void dispatch(vtparse_t *p, vtparse_action_t action, uint8_t ch) {
	p->callback(p, action, ch);
}

static inline bool is_c0(uint8_t ch) {
	return ch < 0x20 && ch != CAN && ch != SUB && ch != ESC;
}

void vtparse_byte(vtparse_t *p, uint8_t ch) {
	// "Anywhere" transitions.
	if (ch == CAN || ch == SUB) {
		dispatch(p, VTPARSE_EXECUTE, ch);
		p->state = GROUND;
		return;
	}
	if (ch == ESC) {
		clear(p);
		p->state = ESCAPE;
		return;
	}
	if (ch >= 0x80 && ch <= 0x9f) {
		if (!p->c1_controls)
			return;
		clear(p);
		switch (ch) {
		case 0x90: case 0x98: case 0x9e: case 0x9f: // DCS SOS PM APC
			p->state = STRING;
			break;
		case 0x9b: // CSI
			p->state = CSI_ENTRY;
			break;
		case 0x9c: // ST
			p->state = GROUND;
			break;
		case 0x9d: // OSC
			p->state = OSC_STRING;
			break;
		default:
			dispatch(p, VTPARSE_EXECUTE, ch);
			p->state = GROUND;
			break;
		}
		return;
	}

	if (p->state == GROUND) {
		dispatch(p, is_c0(ch) ? VTPARSE_EXECUTE : VTPARSE_PRINT, ch);
		return;
	}

	// Outside GROUND, GR bytes act like their GL equivalents.
	uint8_t c = ch >= 0xa0 ? (uint8_t)(ch - 0x80) : ch;

	switch (p->state) {
	case ESCAPE:
		if (is_c0(c))
			dispatch(p, VTPARSE_EXECUTE, c);
		else if (c >= 0x20 && c <= 0x2f) {
			collect(p, c);
			p->state = ESCAPE_INTERMEDIATE;
		} else if (c == '[') {
			clear(p);
			p->state = CSI_ENTRY;
		} else if (c == ']') {
			p->state = OSC_STRING;
		} else if (c == 'P' || c == 'X' || c == '^' || c == '_') {
			p->state = STRING;
		} else if (c != DEL) {
			dispatch(p, VTPARSE_ESC_DISPATCH, c);
			p->state = GROUND;
		}
		break;

	case ESCAPE_INTERMEDIATE:
		if (is_c0(c))
			dispatch(p, VTPARSE_EXECUTE, c);
		else if (c >= 0x20 && c <= 0x2f)
			collect(p, c);
		else if (c != DEL) {
			if (!p->ignoring)
				dispatch(p, VTPARSE_ESC_DISPATCH, c);
			p->state = GROUND;
		}
		break;

	case CSI_ENTRY:
	case CSI_PARAM:
		if (is_c0(c))
			dispatch(p, VTPARSE_EXECUTE, c);
		else if ((c >= '0' && c <= '9') || c == ';') {
			param(p, c);
			p->state = CSI_PARAM;
		} else if (c >= 0x3c && c <= 0x3f && p->state == CSI_ENTRY) {
			p->private_marker = c;
			p->state = CSI_PARAM;
		} else if (c >= 0x3a && c <= 0x3f) {
			p->state = CSI_IGNORE; // ':' or a misplaced private marker
		} else if (c >= 0x20 && c <= 0x2f) {
			collect(p, c);
			p->state = CSI_INTERMEDIATE;
		} else if (c >= 0x40 && c <= 0x7e) {
			dispatch(p, VTPARSE_CSI_DISPATCH, c);
			p->state = GROUND;
		}
		break;

	case CSI_INTERMEDIATE:
		if (is_c0(c))
			dispatch(p, VTPARSE_EXECUTE, c);
		else if (c >= 0x20 && c <= 0x2f)
			collect(p, c);
		else if (c >= 0x30 && c <= 0x3f)
			p->state = CSI_IGNORE;
		else if (c >= 0x40 && c <= 0x7e) {
			if (!p->ignoring)
				dispatch(p, VTPARSE_CSI_DISPATCH, c);
			p->state = GROUND;
		}
		break;

	case CSI_IGNORE:
		if (is_c0(c))
			dispatch(p, VTPARSE_EXECUTE, c);
		else if (c >= 0x40 && c <= 0x7e)
			p->state = GROUND;
		break;

	case OSC_STRING:
	case STRING:
		// Ended by ST, ESC, CAN or SUB (above), BEL in OSC, and, for hosts
		// that never end their strings, by any other C0 control but NUL
		// (padding), which then acts, or by the length limit.
		if (c == BEL && p->state == OSC_STRING)
			p->state = GROUND;
		else if (is_c0(c) && c != 0) {
			p->state = GROUND;
			dispatch(p, VTPARSE_EXECUTE, c);
		} else if (++p->string_len >= VTPARSE_MAX_STRING)
			p->state = GROUND;
		break;
	}
}
