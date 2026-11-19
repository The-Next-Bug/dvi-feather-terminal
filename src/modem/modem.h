#ifndef MODEM_H
#define MODEM_H

// The WiFi modem's start-up (bd xir.17.2, docs/ideas/wifi-modem.md): reset
// the module, find it on the line (its ROM's boot text at 74880, then the
// link rate, else a scan of the rates), move it to the link rate, give it
// the settings the firmware needs (the profile) and the network, save them
// in the module only when something changed. Portable C, host-tested
// against a fake Zimodem (test_modem); apps/terminal/modem_glue.c runs it.
//
// The modem is our Zimodem build (lib/zimodem). While it starts the line is
// the driver's: what arrives goes to modem_feed, nothing else sends.
//
// Zimodem in the Frankless build joins its saved network at boot before it
// says anything, for up to 30 s (WIFI_CONNECT_TRIES), so silence is not a
// wrong rate: noise is (a module talking at another rate: bytes that are
// not text). The driver waits at the link rate, probing, until an OK, noise
// (then it scans the other rates at once) or MODEM_JOIN_WAIT_MS of silence
// (then it scans too); a scan that finds nothing resets the module again.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODEM_ROM_BAUD 74880
#define MODEM_RESET_MS 20        // RST held low
#define MODEM_ROM_MS 300         // the ROM's boot text, at 74880
#define MODEM_BOOT_WAIT_MS 2000  // at the link rate: AT every 500 ms this long, then every MODEM_PROBE_MS
#define MODEM_NOISE 4            // bytes that are not text: the module is at another rate
#define MODEM_SCAN_MS 300        // each rate of the scan
#define MODEM_JOIN_WAIT_MS 35000 // silence at the link rate: Zimodem may be joining its network
#define MODEM_PROBE_MS 2000
#define MODEM_REPLY_MS 2000      // a command's OK
#define MODEM_WIFI_MS 40000      // ATW: Zimodem tries the network for up to 30 s
#define MODEM_ATTEMPTS 2         // resets before giving up

#define MODEM_LINE_MAX 255   // ATI5's line is the longest
#define MODEM_ROM_MAX 79     // the ROM's lines kept: "rst cause:2, boot mode:(3,6)"
#define MODEM_VERSION_MAX 23
#define MODEM_SSID_MAX 32
#define MODEM_IP_MAX 15

typedef struct {
	void *ctx;
	void (*hold_reset)(void *ctx, bool hold); // RST: true = held low
	void (*set_baud)(void *ctx, unsigned baud);
	void (*send)(void *ctx, const char *text); // queued to the line
	bool (*tx_drained)(void *ctx);             // everything queued is out
} modem_io_t;

typedef struct {
	unsigned baud;         // the link rate (BOARD_MODEM_BAUD)
	bool echo;             // command echo: off for terminal mode, on (Hayes) for CP/M
	unsigned dcd_pin;      // Zimodem's GPIO for DCD (S47) and DTR (S55)
	unsigned dtr_pin;
	const char *ssid;      // the network to join; "" leaves the module's own
	const char *password;
} modem_config_t;

typedef enum {
	MODEM_OFF,      // not started
	MODEM_STARTING, // the line is the driver's
	MODEM_READY,    // answering at the link rate, set up
	MODEM_FAILED,   // no answer at any rate after MODEM_ATTEMPTS resets
} modem_state_t;

typedef struct {
	modem_io_t io;
	modem_config_t cfg;
	modem_state_t state;
	uint8_t step;     // within MODEM_STARTING (modem.c)
	uint8_t attempt;  // resets so far
	uint8_t scan;     // index into the scan's rates
	uint8_t noise;    // bytes that were not text, at this rate
	bool replied;     // the awaited OK (or READY.) came
	bool failed;      // ... or ERROR
	bool changed;     // the module's settings changed: AT&W at the end
	bool join_tried;  // ATW sent in this attempt
	uint32_t since;   // when the step began
	uint32_t probed;  // when AT was last sent while waiting
	unsigned baud;    // the rate we are at
	unsigned found;   // the rate the module answered at (0: not yet)
	const char *command; // what was sent (its echo is not a reply)
	char cmd[MODEM_LINE_MAX + 1]; // the last command issued
	char line[MODEM_LINE_MAX + 1];
	size_t len;
	char info[MODEM_LINE_MAX + 1]; // the last line that was not OK/ERROR/the echo
	// What the start-up learned, for the Modem panel, HWINFO and the status line.
	char rom[MODEM_ROM_MAX + 1];         // the ROM's reset cause and boot mode
	char version[MODEM_VERSION_MAX + 1]; // ATI4
	char ssid[MODEM_SSID_MAX + 1];       // ATI3: the network it is set to
	char ip[MODEM_IP_MAX + 1];           // ATI2: "0.0.0.0" when not joined
	const char *problem;                 // the last thing that went wrong, or NULL
} modem_t;

void modem_init(modem_t *m, const modem_io_t *io, const modem_config_t *cfg);

// Reset the module and set it up again (from any state).
void modem_start(modem_t *m, uint32_t now_ms);

// A byte from the line, while MODEM_STARTING.
void modem_feed(modem_t *m, uint8_t c, uint32_t now_ms);

// Call regularly (the main loop): the timeouts and the next step.
void modem_poll(modem_t *m, uint32_t now_ms);

// The line belongs to the start-up.
static inline bool modem_busy(const modem_t *m) {
	return m->state == MODEM_STARTING;
}

// Whether the module has an address (joined a network).
bool modem_joined(const modem_t *m);

// A short description of where the start-up is, for the panel
// ("resetting", "scanning 115200", "ready", "no answer", ...).
const char *modem_step_name(const modem_t *m);

// Whether the network's name and password can be put in an ATW command:
// Zimodem splits them at the first comma and ends the text at a quote.
bool modem_network_sendable(const char *ssid, const char *password);

#endif
