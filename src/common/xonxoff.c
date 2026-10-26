#include "xonxoff.h"

void xonxoff_init(xonxoff_t *x, uint32_t size) {
	*x = (xonxoff_t){.stop = size / 2, .start = size / 8, .step = size / 8};
}

bool xonxoff_received(xonxoff_t *x, uint8_t c) {
	if (c == XONXOFF_XOFF) {
		x->stopped = true;
	} else if (c == XONXOFF_XON) {
		x->stopped = false;
	} else {
		return false;
	}
	return true;
}

uint8_t xonxoff_level(xonxoff_t *x, uint32_t level) {
	if (!x->held && level >= x->stop) {
		x->held = true;
		x->resend = level + x->step;
		return XONXOFF_XOFF;
	}
	if (x->held && level >= x->resend) {
		// The host did not stop (or the XOFF was lost): ask again.
		x->resend = level + x->step;
		return XONXOFF_XOFF;
	}
	if (x->held && level <= x->start) {
		x->held = false;
		return XONXOFF_XON;
	}
	return 0;
}
