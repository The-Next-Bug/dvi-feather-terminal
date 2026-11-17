#ifndef TELNET_H
#define TELNET_H

// Telnet (RFC 854) for a host reached over raw TCP (bd xir.10): Zimodem's
// ATD"host:port" without the T modifier passes the server's bytes straight
// through, so the terminal negotiates itself and can say what Zimodem
// cannot: its real width and its type. Portable C (no pico-sdk),
// host-tested; the terminal runs the host's bytes through telnet_filter
// before term_write, and sends its own through telnet_send_data.
//
// Passive: nothing is sent until the server negotiates. Taken: NAWS
// (the window size, sent again when telnet_set_size changes it), TTYPE
// (telnet_set_type's name), SGA both ways, the server's ECHO; anything else
// is refused (DO -> WONT, WILL -> DONT). Every DO NAWS, DO TTYPE and WILL
// ECHO/SGA is answered, even when already agreed: a new call's server asks
// again and this side cannot see the call end (the negotiation is only
// forgotten by telnet_reset, RIS, or telnet_set_mode).
//
// TELNET_AUTO takes commands out only once one has arrived: an IAC
// followed by WILL, WONT, DO, DONT or SB. Until then an IAC and the byte
// after it are data, as from a plain serial host.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELNET_IAC 255
#define TELNET_DONT 254
#define TELNET_DO 253
#define TELNET_WONT 252
#define TELNET_WILL 251
#define TELNET_SB 250
#define TELNET_SE 240

#define TELNET_OPT_ECHO 1
#define TELNET_OPT_SGA 3 // suppress go-ahead
#define TELNET_OPT_TTYPE 24
#define TELNET_OPT_NAWS 31

#define TELNET_TTYPE_IS 0
#define TELNET_TTYPE_SEND 1

#define TELNET_TYPE_MAX 40 // RFC 1010's names are at most 40 characters

typedef enum {
	TELNET_OFF,  // bytes pass as they are
	TELNET_AUTO, // telnet once the host sends a command
	TELNET_ON,   // telnet from the first byte
} telnet_mode_t;

typedef void (*telnet_send_t)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
	telnet_mode_t mode;
	bool seen;         // a negotiation arrived since the last reset
	bool naws;         // the server asked for the window size (DO NAWS)
	uint32_t will;     // options (< 32) this side said WILL to
	uint32_t dos;      // options (< 32) this side said DO to
	uint8_t state;     // the parser's
	uint8_t verb;      // WILL/WONT/DO/DONT waiting for its option
	uint8_t sb_opt;    // the option of the sub-negotiation being read
	uint8_t sb_len;    // its bytes kept (only the first is looked at)
	uint8_t sb[4];
	unsigned cols, rows; // the window size (telnet_set_size)
	char type[TELNET_TYPE_MAX + 1];
	telnet_send_t send;
	void *ctx;
} telnet_t;

// Off, no negotiation, 80x24, type "VT100". send gets every byte this
// side sends (replies, and telnet_send_data's).
void telnet_init(telnet_t *t, telnet_send_t send, void *ctx);

// A change of mode forgets the negotiation (telnet_reset).
void telnet_set_mode(telnet_t *t, telnet_mode_t mode);

// Forget the negotiation (RIS): no window size updates until the next
// DO NAWS; TELNET_AUTO waits for a command again.
void telnet_reset(telnet_t *t);

// Commands are being taken out of the host's bytes now.
bool telnet_active(const telnet_t *t);

// The name TTYPE sends (cut at TELNET_TYPE_MAX; printable ASCII only).
void telnet_set_type(telnet_t *t, const char *name);

// The window size; sent at once if it changed and the server asked for it.
void telnet_set_size(telnet_t *t, unsigned cols, unsigned rows);

// The host's n bytes in, the data for the terminal in out (room for n + 1
// bytes: an IAC held from the last call may come out with this one's
// first byte); returns the data's length. Replies go to send meanwhile.
size_t telnet_filter(telnet_t *t, const uint8_t *in, size_t n, uint8_t *out);

// This side's bytes to the host, through send: 0xFF doubled while
// telnet_active.
void telnet_send_data(telnet_t *t, const uint8_t *data, size_t len);

#endif
