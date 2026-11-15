#ifndef PRINTJOB_H
#define PRINTJOB_H

// Print jobs through a Zimodem (an ESP8266 WiFi modem, ../Zimodem) on a
// serial line (bd wt6.3). Portable C (no pico-sdk), host-tested
// (test_printjob, against a scripted modem).
//
// The Z80's printer (machine.h, ports 5/6) hands bytes to printjob_put
// while printjob_ready says yes. The first byte after an idle time starts a
// job: the engine takes the modem's attention (a CR, then ATE0Q0V1, as its
// saved settings may be quiet or echoing; AT answered by OK, or after no
// answer the +++ escape with its guard times and ATH, then again), then
// opens the job:
//
// ATS61=n, the idle time, then AT+PRINTA:target (text/plain; Zimodem fixes
// up CR LF and wraps at 80 columns) or AT+PRINTR:target
// (application/octet-stream), target host:port/path. Zimodem says nothing
// when it has connected and ERROR when it could not, so the data starts
// after PRINTJOB_OPEN_US without an ERROR. The job ends when Zimodem has
// read nothing for n seconds: it sends the request's end and OK. The
// engine stops taking data after its own idle time (or printjob_end, OUT
// 5,1) and waits for that OK.
//
// A job does not end with +++: Zimodem times the pluses as it reads them,
// so after a backlog (a printer slow to take the data) they followed the
// data too closely, went to the printer as data and printed. Only the
// attention uses +++, for a modem left in an earlier job. A job that cannot
// be opened, or whose connection is lost, fails: its remaining bytes go to
// the spool hook (printjob_spool_t, NULL: dropped) and the printer stays
// ready, so the Z80 never waits on a dead modem. Data with "+++" after a
// pause of over a second ends a job early (Zimodem's escape).
//
// Raw TCP (JetDirect, port 9100) was tried and dropped (bd wt6): Zimodem's
// stream mode needs +++ to end, and its ATC/ATTn command-mode transmit
// was more trouble than IPP, which works.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PRINTJOB_TARGET_MAX 63 // host:port/path, printable ASCII

// What the printer is (the settings' printer field): off or IPP.
#define PRINTJOB_OFF 0
#define PRINTJOB_IPP 1

// Timing (microseconds).
#define PRINTJOB_GUARD_US 1500000u   // the attention's +++: silence around it (Zimodem wants 900 ms)
#define PRINTJOB_REPLY_US 1000000u   // an AT command's answer
#define PRINTJOB_OPEN_US 2000000u    // IPP: no ERROR this long after AT+PRINT: connected
#define PRINTJOB_FINISH_US 10000000u // OK this long after Zimodem's idle time at most
#define PRINTJOB_PENDING 128         // bytes taken while opening

typedef struct {
	uint8_t kind;       // PRINTJOB_OFF / _IPP
	bool text;          // IPP: AT+PRINTA (text) rather than AT+PRINTR (raw)
	uint16_t idle_s;    // a job ends after this many seconds without a byte
	char target[PRINTJOB_TARGET_MAX + 1];
} printjob_config_t;

// The serial line to the modem.
typedef struct {
	size_t (*room)(void);       // bytes put would take now
	void (*put)(uint8_t c);     // only when room() > 0
	bool (*get)(uint8_t *c);    // a byte from the modem, if one has arrived
	bool (*drained)(void);      // everything put has left the line (the attention's
	                            // +++ guard times count from then); NULL: at once
} printjob_io_t;

// The spool (bd wt6.8, not implemented yet): offered each byte of a job the
// modem cannot take (it failed to open or was lost), and told when that job
// ends, so it could keep the job and send it later. offer returns false if
// it did not keep the byte.
typedef struct {
	bool (*offer)(void *ctx, uint8_t c);
	void (*job_end)(void *ctx);
	void *ctx;
} printjob_spool_t;

typedef enum {
	PRINTJOB_IDLE,     // no job
	PRINTJOB_OPENING,  // taking the modem's attention, opening the job
	PRINTJOB_PRINTING, // the data goes to the modem
	PRINTJOB_CLOSING,  // the job's end: Zimodem's OK
	PRINTJOB_FAILED,   // the job's bytes are dropped (or spooled) until it ends
} printjob_state_t;

typedef enum {
	PRINTJOB_ERR_NONE,
	PRINTJOB_ERR_NO_MODEM, // no OK to AT, even after +++
	PRINTJOB_ERR_REFUSED,  // ERROR to a command, or the open (no connection)
	PRINTJOB_ERR_LOST,     // the connection went during the job
	PRINTJOB_ERR_NO_END,   // no OK from Zimodem at the end
} printjob_error_t;

typedef struct {
	printjob_config_t cfg;
	const printjob_io_t *io;
	const printjob_spool_t *spool; // NULL: failed jobs' bytes are dropped
	printjob_state_t state;
	uint8_t step;        // the state's step (printjob.c)
	uint8_t attempts;    // attention attempts
	uint64_t since_us;   // when the step began
	uint64_t last_us;    // the last byte put
	bool end_asked;      // printjob_end during the open
	// A command being sent, and what came back since.
	char cmd[PRINTJOB_TARGET_MAX + 24];
	uint8_t cmd_len, cmd_pos;
	char reply[48];
	uint8_t reply_len;
	// Bytes taken while opening.
	uint8_t pending[PRINTJOB_PENDING];
	uint16_t pending_len, pending_pos;
	// For the status line and HWINFO.
	printjob_error_t error; // the last job's (NONE once one has worked)
	uint32_t jobs, failed;  // jobs started, of which failed
	uint32_t bytes;         // bytes sent to the modem in jobs
} printjob_t;

void printjob_init(printjob_t *pj, const printjob_io_t *io, const printjob_spool_t *spool);

// New settings: a job running is ended first (as printjob_end).
void printjob_configure(printjob_t *pj, const printjob_config_t *cfg);

// Whether printjob_put would take a byte now: always when off, idle or
// failed; while opening until PRINTJOB_PENDING bytes wait; while printing
// when the line has room.
bool printjob_ready(const printjob_t *pj);

// A byte for the printer (only while printjob_ready; dropped when off).
void printjob_put(printjob_t *pj, uint8_t c, uint64_t now_us);

// End the job now (OUT 5,1) rather than after the idle time.
void printjob_end(printjob_t *pj);

// Run the job: send, read the modem's answers, time out. Often (the main
// loop and while the CPU waits).
void printjob_poll(printjob_t *pj, uint64_t now_us);

// A short name for the error (status line, HWINFO): "no modem", ...
const char *printjob_error_name(printjob_error_t e);

#endif
