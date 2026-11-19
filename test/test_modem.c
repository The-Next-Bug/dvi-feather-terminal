// The WiFi modem's start-up (src/modem/modem.c, bd xir.17.2) against a fake
// Zimodem: one that boots at its saved rate after its ROM's text at 74880,
// may be silent for a while joining its network, answers only bytes sent at
// its rate (and is heard only at it), and keeps a profile, a network and
// what AT&W saved.

#include <stdio.h>
#include <string.h>

#include "carrier.h"
#include "modem.h"
#include "test.h"

#define OUT_MAX 4096

static struct {
	// The module.
	bool powered;        // out of reset
	unsigned rate;       // its UART's
	unsigned saved_rate; // AT&W's (what it boots at)
	bool echo, saved_echo;
	int q, v, f, r, s46, s47, s54, s55, s63;
	int saved_s47, saved_s63;
	char ssid[40], saved_ssid[40], password[70], saved_password[70];
	const char *ap_ssid, *ap_password; // the network that exists
	bool joined;
	uint32_t join_ms;     // how long a join takes
	uint32_t busy_until;  // joining: deaf and silent until then
	unsigned saves;       // AT&W count
	unsigned joins;       // ATW count
	bool dead;            // never answers
	char in[300];
	size_t in_len;
	// The line.
	unsigned our_rate;
	struct {
		uint8_t c;
		unsigned rate;
		uint32_t at;
	} out[OUT_MAX];
	size_t out_head, out_tail;
	bool reset_held;
	unsigned resets;
	char sent[8192]; // everything we sent at the module's rate
} z;

static uint32_t now;
static modem_t m;

// Queue text from the module at its rate, to arrive at `at`.
static void emit_at(const char *text, uint32_t at) {
	for (const char *p = text; *p; ++p) {
		z.out[z.out_head % OUT_MAX].c = (uint8_t)*p;
		z.out[z.out_head % OUT_MAX].rate = z.rate;
		z.out[z.out_head % OUT_MAX].at = at;
		++z.out_head;
	}
}

static void emit(const char *text) {
	emit_at(text, now);
}

static void reply(bool ok) {
	emit(ok ? "\r\nOK\r\n" : "\r\nERROR\r\n");
}

static void boot(void) {
	z.rate = 74880;
	emit_at(" ets Jan  8 2013,rst cause:2, boot mode:(3,6)\r\n", now + 30);
	z.rate = z.saved_rate;
	z.echo = z.saved_echo;
	z.s47 = z.saved_s47;
	z.s63 = z.saved_s63;
	strcpy(z.ssid, z.saved_ssid);
	strcpy(z.password, z.saved_password);
	uint32_t ready = now + 800; // the SDK, the file system: about a second
	z.joined = false;
	if (z.ssid[0]) {
		ready += z.join_ms;
		z.joined = z.ap_ssid && strcmp(z.ssid, z.ap_ssid) == 0 && strcmp(z.password, z.ap_password) == 0;
	}
	z.busy_until = ready;
	emit_at("\r\nZimodem ESP8266 Firmware v4.0.3-frankless\r\n", ready);
	emit_at(z.joined ? "CONNECTED TO x (192.168.1.50)\r\nREADY.\r\n" : "INITIALIZED\r\nREADY.\r\n", ready);
}

static void command(const char *cmd) {
	char line[400];
	if (z.echo) {
		snprintf(line, sizeof line, "%s\r\n", cmd);
		emit(line);
	}
	if (!cmd[0]) {
		return;
	}
	if (strncmp(cmd, "AT", 2) != 0) {
		reply(false);
		return;
	}
	const char *a = cmd + 2;
	if (strcmp(a, "I5") == 0) {
		snprintf(line, sizeof line,
			"ATB%u%sQ%dV%dX1F%dR%dS0=0S40=127S46=%dS47=%dS48=0S49=13S50=0S51=15S54=%dS55=%dS63=%d&P0&O0\r\n",
			z.rate, z.echo ? "E1" : "E0", z.q, z.v, z.f, z.r, z.s46, z.s47, z.s54, z.s55, z.s63);
		emit(line);
		reply(true);
	} else if (strcmp(a, "I4") == 0) {
		emit("4.0.3-frankless\r\n");
		reply(true);
	} else if (strcmp(a, "I3") == 0) {
		snprintf(line, sizeof line, "%s\r\n", z.ssid);
		emit(line);
		reply(true);
	} else if (strcmp(a, "I2") == 0) {
		emit(z.joined ? "192.168.1.50\r\n" : "0.0.0.0\r\n");
		reply(true);
	} else if (a[0] == 'B') {
		unsigned rate;
		CHECK(sscanf(a + 1, "%u", &rate) == 1);
		z.rate = rate;
		reply(true); // at the new rate
	} else if (strcmp(a, "&F") == 0) {
		// Zimodem's factory settings: its config file gone, reloaded at once.
		z.saved_rate = 115200;
		z.saved_echo = true;
		z.saved_s47 = 2;
		z.saved_s63 = 0;
		z.saved_ssid[0] = z.saved_password[0] = '\0';
		z.ssid[0] = z.password[0] = '\0';
		z.joined = false;
		z.rate = 115200;
		z.echo = true;
		reply(true);
	} else if (strcmp(a, "&W") == 0) {
		++z.saves;
		z.saved_rate = z.rate;
		z.saved_echo = z.echo;
		z.saved_s47 = z.s47;
		z.saved_s63 = z.s63;
		strcpy(z.saved_ssid, z.ssid);
		strcpy(z.saved_password, z.password);
		reply(true);
	} else if (a[0] == 'W' && a[1] == '"') {
		++z.joins;
		char ssid[40] = "", password[70] = "";
		const char *comma = strchr(a + 2, ',');
		CHECK(comma != NULL);
		memcpy(ssid, a + 2, (size_t)(comma - (a + 2)));
		const char *quote = strchr(comma + 1, '"');
		memcpy(password, comma + 1, (size_t)(quote - (comma + 1)));
		strcpy(z.ssid, ssid);
		strcpy(z.password, password);
		z.joined = z.ap_ssid && strcmp(ssid, z.ap_ssid) == 0 && strcmp(password, z.ap_password) == 0;
		z.busy_until = now + z.join_ms;
		emit_at(z.joined ? "\r\nOK\r\n" : "\r\nERROR\r\n", z.busy_until);
	} else {
		// Settings: E Q V F R and S<reg>=<value>, any number of them.
		while (*a) {
			char k = *a++;
			int n = 0, v = 0;
			if (sscanf(a, "%d", &n) != 1) {
				reply(false);
				return;
			}
			while (*a >= '0' && *a <= '9') {
				++a;
			}
			if (k == 'S') {
				CHECK(*a == '=');
				sscanf(++a, "%d", &v);
				while (*a >= '0' && *a <= '9') {
					++a;
				}
				int *reg = n == 46 ? &z.s46 : n == 47 ? &z.s47 : n == 54 ? &z.s54 : n == 55 ? &z.s55
					: n == 63 ? &z.s63 : NULL;
				if (reg) {
					*reg = v;
				}
				continue;
			}
			if (k == 'E') {
				z.echo = n != 0;
			} else if (k == 'Q') {
				z.q = n;
			} else if (k == 'V') {
				z.v = n;
			} else if (k == 'F') {
				z.f = n;
			} else if (k == 'R') {
				z.r = n;
			}
		}
		reply(true);
	}
}

static void io_reset(void *ctx, bool hold) {
	(void)ctx;
	if (hold) {
		z.powered = false;
		z.reset_held = true;
		z.out_tail = z.out_head; // whatever it was saying stops
	} else if (z.reset_held) {
		z.reset_held = false;
		++z.resets;
		z.powered = !z.dead;
		if (z.powered) {
			boot();
		}
	}
}

static void io_baud(void *ctx, unsigned baud) {
	(void)ctx;
	z.our_rate = baud;
}

static void io_send(void *ctx, const char *text) {
	(void)ctx;
	for (const char *p = text; *p; ++p) {
		if (!z.powered || z.our_rate != z.rate || (int32_t)(now - z.busy_until) < 0) {
			continue; // not heard
		}
		size_t n = strlen(z.sent);
		if (n + 1 < sizeof z.sent) {
			z.sent[n] = *p;
			z.sent[n + 1] = '\0';
		}
		if (*p == '\r') {
			z.in[z.in_len] = '\0';
			z.in_len = 0;
			command(z.in);
		} else if (z.in_len + 1 < sizeof z.in) {
			z.in[z.in_len++] = *p;
		}
	}
}

static bool io_drained(void *ctx) {
	(void)ctx;
	return true;
}

static const modem_io_t io = {NULL, io_reset, io_baud, io_send, io_drained};

// The factory module: Zimodem's defaults in the Frankless build.
static void fresh_module(void) {
	memset(&z, 0, sizeof z);
	z.saved_rate = 115200;
	z.saved_echo = true;
	z.q = 0;
	z.v = 1;
	z.f = 0;
	z.r = 1;
	z.s46 = 0;
	z.saved_s47 = 2;
	z.s54 = 0;
	z.s55 = 12;
	z.saved_s63 = 0;
	z.join_ms = 3000;
	z.ap_ssid = "Home Net";
	z.ap_password = "secret 1";
	z.powered = true; // running since power-on, before our reset
	z.rate = z.saved_rate;
}

static void start(bool echo, const char *ssid, const char *password) {
	modem_config_t cfg = {.baud = 921600, .echo = echo, .dcd_pin = 4, .dtr_pin = 12, .ssid = ssid,
		.password = password};
	now = 1000;
	modem_init(&m, &io, &cfg);
	modem_start(&m, now);
}

// Run until the start-up ends (or limit ms), a millisecond at a time.
static uint32_t run(uint32_t limit) {
	uint32_t began = now;
	while (modem_busy(&m) && now - began < limit) {
		++now;
		while (z.out_tail != z.out_head && (int32_t)(now - z.out[z.out_tail % OUT_MAX].at) >= 0) {
			// Heard only at its rate; at another, noise our parser drops.
			uint8_t c = z.out[z.out_tail % OUT_MAX].rate == z.our_rate ? z.out[z.out_tail % OUT_MAX].c : 0xfe;
			++z.out_tail;
			modem_feed(&m, c, now);
		}
		modem_poll(&m, now);
	}
	return now - began;
}

static void check_profile(bool echo) {
	CHECK_EQ(z.rate, 921600);
	CHECK_EQ(z.saved_rate, 921600);
	CHECK_EQ(z.echo, echo);
	CHECK_EQ(z.s47, 4);
	CHECK_EQ(z.s63, 2);
	CHECK_EQ(z.saved_s47, 4);
	CHECK_EQ(z.saved_s63, 2);
	CHECK_EQ(z.s46, 0);
	CHECK_EQ(z.s54, 0);
	CHECK_EQ(z.s55, 12);
}

// A module from the factory (115200, echo on, DCD on GPIO2, no network):
// found by the scan, moved to 921600, set up, joined, saved.
static void test_fresh_module(void) {
	fresh_module();
	start(false, "Home Net", "secret 1");
	uint32_t took = run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK(took < 10000);
	CHECK_EQ(m.found, 115200);
	CHECK_EQ(z.resets, 1);
	check_profile(false);
	CHECK(strcmp(z.saved_ssid, "Home Net") == 0);
	CHECK(strcmp(z.saved_password, "secret 1") == 0);
	CHECK_EQ(z.joins, 1);
	CHECK_EQ(z.saves, 1);
	CHECK(modem_joined(&m));
	CHECK(strcmp(m.ip, "192.168.1.50") == 0);
	CHECK(strcmp(m.ssid, "Home Net") == 0);
	CHECK(strcmp(m.version, "4.0.3-frankless") == 0);
	CHECK(strstr(m.rom, "rst cause:2, boot mode:(3,6)") == m.rom);
	CHECK(m.problem == NULL);
	CHECK(strcmp(modem_step_name(&m), "ready") == 0);
	CHECK_EQ(z.our_rate, 921600);
}

// Set up before: found at 921600 after its join, nothing changed, nothing
// saved, no ATW.
static void test_configured_module(void) {
	fresh_module();
	start(false, "Home Net", "secret 1");
	run(60000);
	unsigned saves = z.saves, joins = z.joins;
	modem_start(&m, now);
	uint32_t took = run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK_EQ(m.found, 921600);
	CHECK(took < 5000); // the 3 s join, then at once
	CHECK_EQ(z.saves, saves);
	CHECK_EQ(z.joins, joins);
	CHECK(modem_joined(&m));
	CHECK_EQ(z.resets, 2);
	check_profile(false);
}

// CP/M mode wants Hayes echo; switching back turns it off again.
static void test_echo_follows_mode(void) {
	fresh_module();
	start(true, "Home Net", "secret 1");
	run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	check_profile(true);
	unsigned saves = z.saves;
	start(false, "Home Net", "secret 1");
	run(60000);
	CHECK_EQ(z.echo, false);
	CHECK_EQ(z.saves, saves + 1);
}

// Its network takes 25 s to join at boot, silent meanwhile: waited for at
// the link rate, no scan, no second reset.
static void test_slow_join(void) {
	fresh_module();
	start(false, "Home Net", "secret 1");
	run(60000);
	z.join_ms = 25000;
	modem_start(&m, now);
	uint32_t took = run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK_EQ(m.found, 921600);
	CHECK(took > 25000 && took < 30000);
	CHECK_EQ(z.resets, 2); // no second reset
	CHECK(modem_joined(&m));
}

// Nothing answers: two resets, then given up, at the link rate.
static void test_dead_module(void) {
	fresh_module();
	z.dead = true;
	z.powered = false;
	start(false, "Home Net", "secret 1");
	uint32_t took = run(200000);
	CHECK_EQ(m.state, MODEM_FAILED);
	CHECK_EQ(z.resets, MODEM_ATTEMPTS);
	CHECK(took < MODEM_ATTEMPTS * (MODEM_JOIN_WAIT_MS + 11 * MODEM_SCAN_MS + 1000));
	CHECK(m.problem && strstr(m.problem, "no answer"));
	CHECK(strcmp(modem_step_name(&m), "no answer") == 0);
	CHECK_EQ(z.our_rate, 921600);
	CHECK(!modem_busy(&m));
}

// A wrong password: set up anyway, not joined, said so; the next start
// tries again (ATW each time while not joined).
static void test_wrong_password(void) {
	fresh_module();
	start(false, "Home Net", "wrong");
	run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK(!modem_joined(&m));
	CHECK(m.problem && strstr(m.problem, "join"));
	CHECK_EQ(z.joins, 1);
	check_profile(false);
	start(false, "Home Net", "secret 1");
	run(60000);
	CHECK(modem_joined(&m));
	CHECK(m.problem == NULL);
	CHECK_EQ(z.joins, 2);
}

// No network in our settings: the module keeps its own (none here).
static void test_no_network(void) {
	fresh_module();
	start(false, "", "");
	run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK_EQ(z.joins, 0);
	CHECK(!modem_joined(&m));
	CHECK(m.problem == NULL);
	check_profile(false);
}

// A new password for the network it is on: ATW anyway (forced), no reset.
static void test_join_again(void) {
	fresh_module();
	start(false, "Home Net", "secret 1");
	run(60000);
	CHECK(modem_joined(&m));
	unsigned resets = z.resets, joins = z.joins, saves = z.saves;
	z.ap_password = "secret 2";
	static const char password[] = "secret 2";
	m.cfg.password = password;
	modem_join(&m, now);
	CHECK(modem_busy(&m));
	CHECK(strcmp(modem_step_name(&m), "joining the network") == 0);
	run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK_EQ(z.resets, resets);
	CHECK_EQ(z.joins, joins + 1);
	CHECK_EQ(z.saves, saves + 1);
	CHECK(strcmp(z.saved_password, "secret 2") == 0);
	CHECK(modem_joined(&m));
}

// Defaults: AT&F (the module back at 115200 with nothing set), then the
// whole start-up gives it our profile and network again.
static void test_factory(void) {
	fresh_module();
	start(true, "Home Net", "secret 1");
	run(60000);
	unsigned resets = z.resets;
	modem_factory(&m, now);
	CHECK(strcmp(modem_step_name(&m), "restoring its factory settings") == 0);
	run(80000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK_EQ(z.resets, resets + 1);
	CHECK_EQ(m.found, 115200);
	check_profile(true);
	CHECK(strcmp(z.saved_ssid, "Home Net") == 0);
	CHECK(modem_joined(&m));
	// One that is not ready is only started over.
	fresh_module();
	z.dead = true;
	z.powered = false;
	start(false, "Home Net", "secret 1");
	run(200000);
	CHECK_EQ(m.state, MODEM_FAILED);
	z.dead = false;
	modem_factory(&m, now);
	run(60000);
	CHECK_EQ(m.state, MODEM_READY);
}

// What ATW cannot carry: a comma or quote in the name, a quote in the
// password.
static void test_network_sendable(void) {
	CHECK(modem_network_sendable("Home Net", "a,b c"));
	CHECK(!modem_network_sendable("a,b", "x"));
	CHECK(!modem_network_sendable("a\"b", "x"));
	CHECK(!modem_network_sendable("ab", "x\"y"));
	CHECK(!modem_network_sendable("", "x"));
	fresh_module();
	start(false, "Bad,Name", "x");
	run(60000);
	CHECK_EQ(m.state, MODEM_READY);
	CHECK_EQ(z.joins, 0);
	CHECK(m.problem && strstr(m.problem, "comma"));
}

// The carrier (bd xir.17.5): DCD debounced; a hang-up holds DTR inactive
// until DCD drops (then at least CARRIER_DTR_MS), or gives up after
// CARRIER_HANGUP_MS.
static void test_carrier(void) {
	carrier_t c;
	carrier_init(&c, false, 0);
	CHECK(c.dtr);
	CHECK(!c.up);
	CHECK_EQ(carrier_poll(&c, true, 10), CARRIER_NONE);
	CHECK_EQ(carrier_poll(&c, false, 30), CARRIER_NONE); // a glitch
	CHECK_EQ(carrier_poll(&c, false, 200), CARRIER_NONE);
	CHECK(!c.up);
	CHECK_EQ(carrier_poll(&c, true, 1000), CARRIER_NONE);
	CHECK_EQ(carrier_poll(&c, true, 1000 + CARRIER_DEBOUNCE_MS), CARRIER_UP);
	CHECK(c.up);
	// The far end closes.
	CHECK_EQ(carrier_poll(&c, false, 5000), CARRIER_NONE);
	CHECK_EQ(carrier_poll(&c, false, 5000 + CARRIER_DEBOUNCE_MS), CARRIER_LOST);
	CHECK(c.dtr);

	// A hang-up the modem takes: DCD drops 30 ms after DTR.
	carrier_init(&c, true, 0);
	carrier_hangup(&c, 100);
	CHECK(!c.dtr);
	carrier_hangup(&c, 110); // again: no change
	CHECK_EQ(c.hangup_since, 100);
	CHECK_EQ(carrier_poll(&c, true, 120), CARRIER_NONE);
	CHECK_EQ(carrier_poll(&c, false, 130), CARRIER_NONE);
	CHECK_EQ(carrier_poll(&c, false, 130 + CARRIER_DEBOUNCE_MS), CARRIER_LOST);
	CHECK(!c.dtr); // DTR held CARRIER_DTR_MS at least
	CHECK_EQ(carrier_poll(&c, false, 100 + CARRIER_DTR_MS - 1), CARRIER_NONE);
	CHECK_EQ(carrier_poll(&c, false, 100 + CARRIER_DTR_MS), CARRIER_HUNG_UP);
	CHECK(c.dtr);
	CHECK_EQ(carrier_poll(&c, false, 1000), CARRIER_NONE);

	// One it does not: DCD stays; DTR back, the caller's +++ next.
	carrier_init(&c, true, 0);
	carrier_hangup(&c, 0);
	CHECK_EQ(carrier_poll(&c, true, CARRIER_HANGUP_MS - 1), CARRIER_NONE);
	CHECK(!c.dtr);
	CHECK_EQ(carrier_poll(&c, true, CARRIER_HANGUP_MS), CARRIER_STUCK);
	CHECK(c.dtr);
	CHECK(c.up);

	// Nothing to drop: done after the DTR pulse.
	carrier_init(&c, false, 0);
	carrier_hangup(&c, 0);
	CHECK_EQ(carrier_poll(&c, false, CARRIER_DTR_MS), CARRIER_HUNG_UP);
	CHECK(c.dtr);
}

int main(void) {
	RUN(test_fresh_module);
	RUN(test_configured_module);
	RUN(test_echo_follows_mode);
	RUN(test_slow_join);
	RUN(test_dead_module);
	RUN(test_wrong_password);
	RUN(test_no_network);
	RUN(test_network_sendable);
	RUN(test_join_again);
	RUN(test_factory);
	RUN(test_carrier);
	return tests_done();
}
