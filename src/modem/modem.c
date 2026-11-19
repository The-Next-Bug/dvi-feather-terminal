#include "modem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The start-up's steps, in order (some skipped).
enum {
	ST_RESET,       // RST held low
	ST_ROM,         // at 74880: the ROM's boot text
	ST_BOOT,        // at the link rate: Zimodem's READY. or an OK
	ST_SCAN,        // each other rate in turn
	ST_RATE_SET,    // found elsewhere: ATB<link rate>, then switch
	ST_RATE_CHECK,  // and an OK at the link rate
	ST_SETTLE,      // after a probe's OK: let a second OK (the bare CR's) go by
	ST_PROFILE_ASK, // ATI5: the settings in use
	ST_PROFILE_SET, // the ones that differ
	ST_SSID_ASK,    // ATI3: the network it is set to
	ST_IP_ASK,      // ATI2: its address
	ST_JOIN,        // ATW"ssid,password"
	ST_SAVE,        // AT&W, if anything changed
	ST_VERSION,     // ATI4
	ST_FACTORY,     // AT&F sent: wait, then the reset
};

#define SETTLE_MS 150
#define FIRST_PROBE_MS 1000 // into MODEM_BOOT_WAIT_MS: the module may still be talking
#define RATE_PROBE_MS 500

// The rates tried after the link rate: the Frankless build's default first,
// then the ROM's (a module stuck there), then the rest, fastest first.
static const unsigned scan_rates[] = {115200, MODEM_ROM_BAUD, 460800, 230400, 57600, 38400, 19200, 9600, 2400,
	1200};
#define SCAN_RATES (sizeof scan_rates / sizeof scan_rates[0])

static void copy_text(char *dst, size_t max, const char *src) {
	size_t n = strlen(src);
	if (n > max) {
		n = max;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void to_step(modem_t *m, uint8_t step, uint32_t now) {
	m->step = step;
	m->since = now;
	m->probed = now;
	m->replied = m->failed = false;
}

static void set_baud(modem_t *m, unsigned baud) {
	m->baud = baud;
	m->len = 0;
	m->noise = 0;
	m->io.set_baud(m->io.ctx, baud);
}

// A command (without its CR), whose echo is then not taken as a reply.
static void issue(modem_t *m, uint8_t step, const char *command, uint32_t now) {
	to_step(m, step, now);
	if (command != m->cmd) {
		copy_text(m->cmd, MODEM_LINE_MAX, command);
	}
	m->command = m->cmd;
	m->info[0] = '\0';
	m->io.send(m->io.ctx, m->cmd);
	m->io.send(m->io.ctx, "\r");
}

// AT after a bare CR, which ends whatever was half typed (the CR alone may
// get an OK of its own: ST_SETTLE lets it go by).
static void probe(modem_t *m, uint32_t now) {
	m->probed = now;
	m->command = "AT";
	m->io.send(m->io.ctx, "\rAT\r");
}

// The awaited reply came (or ERROR), or the time is up.
static bool done(const modem_t *m, uint32_t now, uint32_t timeout) {
	return m->replied || m->failed || now - m->since >= timeout;
}

void modem_init(modem_t *m, const modem_io_t *io, const modem_config_t *cfg) {
	*m = (modem_t){.io = *io, .cfg = *cfg};
}

static void start_attempt(modem_t *m, uint32_t now) {
	m->state = MODEM_STARTING;
	m->found = 0;
	m->changed = false;
	m->join_tried = false;
	m->rom[0] = '\0';
	m->len = 0;
	to_step(m, ST_RESET, now);
	m->io.hold_reset(m->io.ctx, true);
}

void modem_start(modem_t *m, uint32_t now_ms) {
	m->attempt = 0;
	m->problem = NULL;
	m->version[0] = m->ssid[0] = m->ip[0] = '\0';
	start_attempt(m, now_ms);
}

static void next_attempt(modem_t *m, uint32_t now, const char *problem) {
	m->problem = problem;
	if (++m->attempt < MODEM_ATTEMPTS) {
		start_attempt(m, now);
	} else {
		m->state = MODEM_FAILED;
		set_baud(m, m->cfg.baud);
	}
}

// The ROM's lines: keep the one with the reset cause (else the last).
static void rom_line(modem_t *m, const char *line) {
	if (!strstr(m->rom, "rst cause")) {
		const char *cause = strstr(line, "rst cause");
		copy_text(m->rom, MODEM_ROM_MAX, cause ? cause : line);
	}
}

static void handle_line(modem_t *m, const char *line) {
	if (m->step == ST_ROM) {
		rom_line(m, line);
		return;
	}
	if (strcmp(line, "OK") == 0 ||
		(strcmp(line, "READY.") == 0 && m->step == ST_BOOT)) {
		m->replied = true;
	} else if (strcmp(line, "ERROR") == 0) {
		m->failed = true;
	} else if (!m->command || strcmp(line, m->command) != 0) {
		copy_text(m->info, MODEM_LINE_MAX, line);
	}
}

void modem_feed(modem_t *m, uint8_t c, uint32_t now_ms) {
	(void)now_ms;
	if (m->state != MODEM_STARTING) {
		return;
	}
	if (c == '\r' || c == '\n') {
		if (m->len) {
			m->line[m->len] = '\0';
			m->len = 0;
			handle_line(m, m->line);
		}
	} else if (c >= 0x20 && c < 0x7f) {
		if (m->len < MODEM_LINE_MAX) {
			m->line[m->len++] = (char)c;
		}
	} else if (m->noise < 255) {
		++m->noise;
	}
}

// The profile: what ATI5 shows of it.
typedef struct {
	int e, q, v, f, r;
	int s46, s47, s54, s55, s63;
} profile_t;

// ATI5's line: "ATB921600E1Q0V1X1F0R1&D...S46=0S47=4...S63=0&P0&O0", fields
// as letter and digits, &letter and digits, S<reg>=<value>. False if it is
// not such a line.
static bool parse_profile(const char *p, profile_t *out) {
	*out = (profile_t){-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	const char *at = strstr(p, "ATB");
	if (!at) {
		return false;
	}
	p = at + 2;
	while (*p) {
		char k = *p++;
		if (k == '&') {
			if (*p) {
				++p;
			}
			while (*p >= '0' && *p <= '9') {
				++p;
			}
			continue;
		}
		char *end;
		long n = strtol(p, &end, 10);
		if (end == p) {
			continue; // a letter without a number
		}
		p = end;
		if (k == 'S' && *p == '=') {
			long v = strtol(p + 1, &end, 10);
			p = end;
			int *reg = n == 46 ? &out->s46 : n == 47 ? &out->s47 : n == 54 ? &out->s54 : n == 55 ? &out->s55
				: n == 63 ? &out->s63 : NULL;
			if (reg) {
				*reg = (int)v;
			}
			continue;
		}
		int *f = k == 'E' ? &out->e : k == 'Q' ? &out->q : k == 'V' ? &out->v : k == 'F' ? &out->f
			: k == 'R' ? &out->r : NULL;
		if (f) {
			*f = (int)n;
		}
	}
	return true;
}

static void add(char *cmd, int have, int want, const char *fmt) {
	if (have != want) {
		size_t n = strlen(cmd);
		snprintf(cmd + n, MODEM_LINE_MAX + 1 - n, fmt, want);
	}
}

// The command setting what differs from the profile (have), or "" when
// nothing does: result codes as our parsers read them (Q0 V1, CR LF),
// RTS/CTS, DCD and DTR on our pins, active low (S46/S54 0), DTR hang-up
// (S63=2: safe, as the firmware drives DTR), the mode's echo.
static void profile_command(const modem_t *m, const profile_t *have, char *cmd) {
	strcpy(cmd, "AT");
	add(cmd, have->e, m->cfg.echo ? 1 : 0, "E%d");
	add(cmd, have->q, 0, "Q%d");
	add(cmd, have->v, 1, "V%d");
	add(cmd, have->f, 0, "F%d");
	add(cmd, have->r, 1, "R%d");
	add(cmd, have->s46, 0, "S46=%d");
	add(cmd, have->s47, (int)m->cfg.dcd_pin, "S47=%d");
	add(cmd, have->s54, 0, "S54=%d");
	add(cmd, have->s55, (int)m->cfg.dtr_pin, "S55=%d");
	add(cmd, have->s63, 2, "S63=%d");
	if (strcmp(cmd, "AT") == 0) {
		cmd[0] = '\0';
	}
}

bool modem_network_sendable(const char *ssid, const char *password) {
	return ssid[0] && !strpbrk(ssid, ",\"") && !strchr(password, '"');
}

bool modem_joined(const modem_t *m) {
	return m->ip[0] && strcmp(m->ip, "0.0.0.0") != 0;
}

// The network: ours, if set, unless the module is on it already.
static void network_step(modem_t *m, uint32_t now) {
	const char *want = m->cfg.ssid;
	if (want[0] && !m->join_tried && (m->force_join || strcmp(m->ssid, want) != 0 || !modem_joined(m))) {
		if (modem_network_sendable(want, m->cfg.password)) {
			m->join_tried = true;
			m->force_join = false;
			snprintf(m->cmd, sizeof m->cmd, "ATW\"%s,%s\"", want, m->cfg.password);
			issue(m, ST_JOIN, m->cmd, now);
			return;
		}
		m->problem = "the network's name or password has a comma or quote";
	}
	if (m->changed) {
		issue(m, ST_SAVE, "AT&W", now);
	} else {
		issue(m, ST_VERSION, "ATI4", now);
	}
}

void modem_join(modem_t *m, uint32_t now_ms) {
	m->force_join = true;
	if (m->state != MODEM_READY) {
		modem_start(m, now_ms);
		return;
	}
	m->state = MODEM_STARTING;
	m->problem = NULL;
	m->join_tried = false;
	m->changed = false;
	network_step(m, now_ms);
}

void modem_factory(modem_t *m, uint32_t now_ms) {
	m->force_join = true;
	if (m->state != MODEM_READY) {
		modem_start(m, now_ms);
		return;
	}
	m->state = MODEM_STARTING;
	issue(m, ST_FACTORY, "AT&F", now_ms);
}

static void ready(modem_t *m) {
	m->state = MODEM_READY;
	m->command = NULL;
}

void modem_poll(modem_t *m, uint32_t now_ms) {
	if (m->state != MODEM_STARTING) {
		return;
	}
	uint32_t now = now_ms, elapsed = now - m->since;
	switch (m->step) {
	case ST_RESET:
		if (elapsed >= MODEM_RESET_MS) {
			set_baud(m, MODEM_ROM_BAUD);
			m->io.hold_reset(m->io.ctx, false);
			to_step(m, ST_ROM, now);
		}
		break;
	case ST_ROM:
		if (elapsed >= MODEM_ROM_MS) {
			if (m->len) { // a last line without its end
				m->line[m->len] = '\0';
				rom_line(m, m->line);
			}
			set_baud(m, m->cfg.baud);
			to_step(m, ST_BOOT, now);
		}
		break;
	case ST_BOOT:
		// Silence: it may be joining its network. Noise: another rate.
		if (m->replied) {
			m->found = m->cfg.baud;
			to_step(m, ST_SETTLE, now);
		} else if (m->noise >= MODEM_NOISE || elapsed >= MODEM_JOIN_WAIT_MS) {
			m->scan = 0;
			to_step(m, ST_SCAN, now);
			m->since = now - MODEM_SCAN_MS; // the first rate at once
		} else if (elapsed >= FIRST_PROBE_MS &&
			now - m->probed >= (elapsed < MODEM_BOOT_WAIT_MS ? RATE_PROBE_MS : MODEM_PROBE_MS)) {
			probe(m, now);
		}
		break;
	case ST_SCAN:
		if (m->replied) {
			m->found = m->baud;
			char cmd[16];
			snprintf(cmd, sizeof cmd, "ATB%u", m->cfg.baud);
			issue(m, ST_RATE_SET, cmd, now); // Zimodem answers it at the new rate
			break;
		}
		if (elapsed >= MODEM_SCAN_MS) {
			while (m->scan < SCAN_RATES && scan_rates[m->scan] == m->cfg.baud) {
				++m->scan;
			}
			if (m->scan == SCAN_RATES) {
				next_attempt(m, now, "no answer at any rate");
				break;
			}
			set_baud(m, scan_rates[m->scan++]);
			to_step(m, ST_SCAN, now);
			probe(m, now);
		}
		break;
	case ST_RATE_SET:
		if (m->io.tx_drained(m->io.ctx)) {
			set_baud(m, m->cfg.baud); // what came at the old rate is dropped
			to_step(m, ST_RATE_CHECK, now);
		}
		break;
	case ST_RATE_CHECK:
		// ATB's own OK (at the new rate), or a probe's.
		if (m->replied) {
			m->changed = true;
			to_step(m, ST_SETTLE, now);
		} else if (elapsed >= MODEM_REPLY_MS) {
			next_attempt(m, now, "it did not answer after the rate change");
		} else if (elapsed >= SETTLE_MS && now - m->probed >= RATE_PROBE_MS) {
			probe(m, now);
		}
		break;
	case ST_SETTLE:
		if (elapsed >= SETTLE_MS) {
			issue(m, ST_PROFILE_ASK, "ATI5", now);
		}
		break;
	case ST_PROFILE_ASK:
		if (done(m, now, MODEM_REPLY_MS)) {
			profile_t have;
			if (!m->replied || !parse_profile(m->info, &have)) {
				have = (profile_t){-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // set all of it
			}
			char cmd[MODEM_LINE_MAX + 1];
			profile_command(m, &have, cmd);
			if (cmd[0]) {
				issue(m, ST_PROFILE_SET, cmd, now);
			} else {
				issue(m, ST_SSID_ASK, "ATI3", now);
			}
		}
		break;
	case ST_PROFILE_SET:
		if (done(m, now, MODEM_REPLY_MS)) {
			if (m->replied) {
				m->changed = true;
			} else {
				m->problem = "it refused the settings";
			}
			issue(m, ST_SSID_ASK, "ATI3", now);
		}
		break;
	case ST_SSID_ASK:
		if (done(m, now, MODEM_REPLY_MS)) {
			copy_text(m->ssid, MODEM_SSID_MAX, m->replied ? m->info : "");
			issue(m, ST_IP_ASK, "ATI2", now);
		}
		break;
	case ST_IP_ASK:
		if (done(m, now, MODEM_REPLY_MS)) {
			copy_text(m->ip, MODEM_IP_MAX, m->replied ? m->info : "");
			network_step(m, now);
		}
		break;
	case ST_JOIN:
		if (done(m, now, MODEM_WIFI_MS)) {
			if (m->replied) {
				m->changed = true;
				copy_text(m->ssid, MODEM_SSID_MAX, m->cfg.ssid);
			} else {
				m->problem = "it did not join the network";
			}
			issue(m, ST_IP_ASK, "ATI2", now); // then the save (join_tried)
		}
		break;
	case ST_SAVE:
		if (done(m, now, MODEM_REPLY_MS)) {
			if (!m->replied) {
				m->problem = "it did not save its settings";
			}
			m->changed = false;
			issue(m, ST_VERSION, "ATI4", now);
		}
		break;
	case ST_VERSION:
		if (done(m, now, MODEM_REPLY_MS)) {
			copy_text(m->version, MODEM_VERSION_MAX, m->replied ? m->info : "");
			ready(m);
		}
		break;
	case ST_FACTORY:
		if (elapsed >= MODEM_FACTORY_MS) {
			bool force = m->force_join;
			modem_start(m, now);
			m->force_join = force;
		}
		break;
	default:
		break;
	}
}

const char *modem_step_name(const modem_t *m) {
	switch (m->state) {
	case MODEM_OFF:
		return "off";
	case MODEM_READY:
		return "ready";
	case MODEM_FAILED:
		return "no answer";
	default:
		break;
	}
	switch (m->step) {
	case ST_RESET:
	case ST_ROM:
		return "resetting";
	case ST_BOOT:
		return "starting"; // silent: maybe joining its network
	case ST_SCAN:
	case ST_RATE_SET:
	case ST_RATE_CHECK:
		return "finding its rate";
	case ST_JOIN:
		return "joining the network";
	case ST_FACTORY:
		return "restoring its factory settings";
	default:
		return "setting it up";
	}
}
