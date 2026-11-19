// Print jobs through a Zimodem on a serial line (printjob.h, bd wt6.3).

#include "printjob.h"

#include <stdio.h>
#include <string.h>

// The steps of the states that talk to the modem.
enum {
	// PRINTJOB_OPENING
	OPEN_WAKE,      // CR ATE0Q0V1 CR sent, its answer let pass
	OPEN_PROBE,     // AT: OK?
	OPEN_ESC_GUARD, // no answer: silence, then +++
	OPEN_ESC_WAIT,  // silence after +++, then ATH and wake again
	OPEN_TIMEOUT,   // IPP: ATS61=n: OK?
	OPEN_PRINT,     // AT+PRINT: no ERROR in time
	// PRINTJOB_CLOSING
	CLOSE_WAIT,     // Zimodem's own end of the job (S61): OK?
};

#define WAKE_US 300000u // the answer to ATE0Q0V1 (whatever it is) lets pass
#define ATTEMPTS 2      // attention: AT, then +++ and AT again

void printjob_init(printjob_t *pj, const printjob_io_t *io, const printjob_spool_t *spool) {
	memset(pj, 0, sizeof *pj);
	pj->io = io;
	pj->spool = spool;
	pj->cfg.idle_s = 10;
}

void printjob_configure(printjob_t *pj, const printjob_config_t *cfg) {
	// A job running is dropped: the next one's attention (+++, ATH) gets the
	// modem back whatever state this leaves it in.
	if (pj->state == PRINTJOB_FAILED && pj->spool && pj->spool->job_end) {
		pj->spool->job_end(pj->spool->ctx);
	}
	pj->state = PRINTJOB_IDLE;
	pj->cfg = *cfg;
	pj->cfg.target[PRINTJOB_TARGET_MAX] = '\0';
	if (!pj->cfg.idle_s) {
		pj->cfg.idle_s = 1;
	}
}

static bool pending_left(const printjob_t *pj) {
	return pj->pending_pos < pj->pending_len;
}

bool printjob_ready(const printjob_t *pj) {
	switch (pj->state) {
	case PRINTJOB_OPENING:
		return pj->pending_len < PRINTJOB_PENDING;
	case PRINTJOB_PRINTING:
		return !pending_left(pj) && pj->io->room() > 0;
	case PRINTJOB_CLOSING: // the next job's bytes wait for this one's end
		return false;
	default:
		return true;
	}
}

// Start sending a command; the step's time starts when it has gone.
static void send(printjob_t *pj, const char *s) {
	size_t n = strlen(s);
	if (n > sizeof pj->cmd) {
		n = sizeof pj->cmd;
	}
	memcpy(pj->cmd, s, n);
	pj->cmd_len = (uint8_t)n;
	pj->cmd_pos = 0;
	pj->reply_len = 0;
	pj->reply[0] = '\0';
}

static bool sending(const printjob_t *pj) {
	return pj->cmd_pos < pj->cmd_len;
}

static bool drained(const printjob_t *pj) {
	return !pj->io->drained || pj->io->drained();
}

static bool replied(const printjob_t *pj, const char *s) {
	return strstr(pj->reply, s) != NULL;
}

static void step(printjob_t *pj, uint8_t s, uint64_t now_us) {
	pj->step = s;
	pj->since_us = now_us;
}

static void fail(printjob_t *pj, printjob_error_t e, uint64_t now_us) {
	pj->state = PRINTJOB_FAILED;
	pj->error = e;
	++pj->failed;
	pj->cmd_len = pj->cmd_pos = 0;
	// What was taken but not sent goes the way of the rest of the job.
	while (pending_left(pj)) {
		uint8_t c = pj->pending[pj->pending_pos++];
		if (pj->spool && pj->spool->offer) {
			pj->spool->offer(pj->spool->ctx, c);
		}
	}
	pj->pending_len = pj->pending_pos = 0;
	pj->last_us = now_us; // the idle time from here: the Z80 may have stopped meanwhile
}

static void done(printjob_t *pj, printjob_error_t e) {
	pj->state = PRINTJOB_IDLE;
	pj->error = e;
	if (e != PRINTJOB_ERR_NONE) {
		++pj->failed;
	}
}

static void wake(printjob_t *pj, uint64_t now_us) {
	send(pj, "\rATE0Q0V1\r");
	step(pj, OPEN_WAKE, now_us);
}

static void start_printing(printjob_t *pj) {
	pj->state = PRINTJOB_PRINTING;
	pj->reply_len = 0;
	pj->reply[0] = '\0';
}

void printjob_put(printjob_t *pj, uint8_t c, uint64_t now_us) {
	if (pj->cfg.kind == PRINTJOB_OFF) {
		return;
	}
	pj->last_us = now_us;
	switch (pj->state) {
	case PRINTJOB_IDLE:
		++pj->jobs;
		pj->state = PRINTJOB_OPENING;
		pj->attempts = 0;
		pj->end_asked = false;
		pj->pending_len = pj->pending_pos = 0;
		pj->pending[pj->pending_len++] = c;
		wake(pj, now_us);
		break;
	case PRINTJOB_OPENING:
		if (pj->pending_len < PRINTJOB_PENDING) {
			pj->pending[pj->pending_len++] = c;
		}
		break;
	case PRINTJOB_PRINTING:
		if (!pending_left(pj) && pj->io->room() > 0) {
			pj->io->put(c);
			++pj->bytes;
		}
		break;
	case PRINTJOB_FAILED:
		if (pj->spool && pj->spool->offer) {
			pj->spool->offer(pj->spool->ctx, c);
		}
		break;
	default:
		break;
	}
}

void printjob_end(printjob_t *pj) {
	if (pj->state != PRINTJOB_IDLE) {
		pj->end_asked = true;
	}
}

static void read_replies(printjob_t *pj) {
	uint8_t c;
	while (pj->io->get(&c)) {
		if (c == 0) {
			continue;
		}
		if (pj->reply_len == sizeof pj->reply - 1) { // keep the newer half
			size_t half = (sizeof pj->reply - 1) / 2;
			memmove(pj->reply, pj->reply + half, pj->reply_len - half);
			pj->reply_len = (uint8_t)(pj->reply_len - half);
		}
		pj->reply[pj->reply_len++] = (char)c;
		pj->reply[pj->reply_len] = '\0';
	}
}

static void opening(printjob_t *pj, uint64_t now_us) {
	uint64_t waited = now_us - pj->since_us;
	char line[sizeof pj->cmd];
	switch (pj->step) {
	case OPEN_WAKE:
		if (waited >= WAKE_US) {
			send(pj, "AT\r");
			step(pj, OPEN_PROBE, now_us);
		}
		break;
	case OPEN_PROBE:
		if (replied(pj, "OK")) {
			// Zimodem ends the job by itself after this long without data,
			// timed as it reads the bytes: the job's end.
			snprintf(line, sizeof line, "ATS61=%u\r", (unsigned)pj->cfg.idle_s);
			send(pj, line);
			step(pj, OPEN_TIMEOUT, now_us);
		} else if (waited >= PRINTJOB_REPLY_US) {
			if (++pj->attempts >= ATTEMPTS) {
				fail(pj, PRINTJOB_ERR_NO_MODEM, now_us);
			} else {
				step(pj, OPEN_ESC_GUARD, now_us); // in a stream or a print job?
			}
		}
		break;
	case OPEN_ESC_GUARD:
		if (!drained(pj)) {
			pj->since_us = now_us;
		} else if (waited >= PRINTJOB_GUARD_US) {
			send(pj, "+++");
			step(pj, OPEN_ESC_WAIT, now_us);
		}
		break;
	case OPEN_ESC_WAIT:
		if (waited >= PRINTJOB_GUARD_US) {
			// Nothing should stay connected (TTY2 is the printer's), then
			// the wake-up again.
			send(pj, "ATH\r\rATE0Q0V1\r");
			step(pj, OPEN_WAKE, now_us);
		}
		break;
	case OPEN_TIMEOUT:
		if (replied(pj, "OK")) {
			snprintf(line, sizeof line, "AT+PRINT%c:%s\r", pj->cfg.text ? 'A' : 'R', pj->cfg.target);
			send(pj, line);
			step(pj, OPEN_PRINT, now_us);
		} else if (replied(pj, "ERROR")) {
			fail(pj, PRINTJOB_ERR_REFUSED, now_us);
		} else if (waited >= PRINTJOB_REPLY_US) {
			fail(pj, PRINTJOB_ERR_NO_MODEM, now_us);
		}
		break;
	case OPEN_PRINT:
		if (replied(pj, "ERROR")) {
			fail(pj, PRINTJOB_ERR_REFUSED, now_us);
		} else if ((pj->io->carrier && pj->io->carrier()) || waited >= PRINTJOB_OPEN_US) {
			start_printing(pj);
		}
		break;
	default:
		break;
	}
}

static bool job_over(const printjob_t *pj, uint64_t now_us) {
	return pj->end_asked || now_us - pj->last_us >= (uint64_t)pj->cfg.idle_s * 1000000u;
}

static void printing(printjob_t *pj, uint64_t now_us) {
	while (pending_left(pj) && pj->io->room() > 0) {
		pj->io->put(pj->pending[pj->pending_pos++]);
		++pj->bytes;
	}
	// Zimodem leaves a print job with ERROR when the printer's connection
	// goes; OK means it ended the job itself (S61), so the modem is in
	// command mode and must get no more data.
	if (replied(pj, "ERROR")) {
		fail(pj, PRINTJOB_ERR_LOST, now_us);
	} else if (replied(pj, "OK")) {
		done(pj, PRINTJOB_ERR_NONE);
	} else if (!pending_left(pj) && job_over(pj, now_us)) {
		// Zimodem's idle time is ours, timed from when it read the last
		// byte: it ends the job a little after us.
		pj->state = PRINTJOB_CLOSING;
		step(pj, CLOSE_WAIT, now_us);
	}
}

static void closing(printjob_t *pj, uint64_t now_us) {
	uint64_t waited = now_us - pj->since_us;
	switch (pj->step) {
	case CLOSE_WAIT:
		if (replied(pj, "OK")) {
			done(pj, PRINTJOB_ERR_NONE);
		} else if (replied(pj, "ERROR")) {
			done(pj, PRINTJOB_ERR_LOST);
		} else if (waited >= (uint64_t)pj->cfg.idle_s * 1000000u + PRINTJOB_FINISH_US) {
			done(pj, PRINTJOB_ERR_NO_END); // the next job's attention gets the modem back
		}
		break;
	default:
		break;
	}
}

void printjob_poll(printjob_t *pj, uint64_t now_us) {
	if (pj->cfg.kind == PRINTJOB_OFF) {
		pj->state = PRINTJOB_IDLE;
		return;
	}
	while (sending(pj) && pj->io->room() > 0) {
		pj->io->put((uint8_t)pj->cmd[pj->cmd_pos++]);
		if (!sending(pj)) {
			pj->since_us = now_us; // the step's time from the command's end
		}
	}
	read_replies(pj);
	if (sending(pj)) {
		return;
	}
	switch (pj->state) {
	case PRINTJOB_OPENING:
		opening(pj, now_us);
		break;
	case PRINTJOB_PRINTING:
		printing(pj, now_us);
		break;
	case PRINTJOB_CLOSING:
		closing(pj, now_us);
		break;
	case PRINTJOB_FAILED:
		if (job_over(pj, now_us)) {
			if (pj->spool && pj->spool->job_end) {
				pj->spool->job_end(pj->spool->ctx);
			}
			pj->state = PRINTJOB_IDLE;
		}
		break;
	default:
		break;
	}
}

const char *printjob_error_name(printjob_error_t e) {
	switch (e) {
	case PRINTJOB_ERR_NONE:
		return "OK";
	case PRINTJOB_ERR_NO_MODEM:
		return "no modem";
	case PRINTJOB_ERR_REFUSED:
		return "refused";
	case PRINTJOB_ERR_LOST:
		return "connection lost";
	case PRINTJOB_ERR_NO_END:
		return "no end";
	}
	return "?";
}
