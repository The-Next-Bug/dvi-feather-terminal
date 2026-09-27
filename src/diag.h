#ifndef DIAG_H
#define DIAG_H

// Bring-up diagnostics for VIDEO_DIAG builds, on UART0 TX (115200 8N1) and
// the red LED. All calls are no-ops in normal builds.

#include <stdint.h>

#ifndef VIDEO_DIAG
#define VIDEO_DIAG 0
#endif

// Start UART0 TX at the current clock and print a boot banner. Call first
// thing in main(), before the clock changes; call diag_reinit() after.
void diag_init(void);
void diag_reinit(uint32_t baud);

// Print "diag: <text>" on its own line.
void diag_mark(const char *text);

// Toggle the LED; call about twice a second from the main loop.
void diag_heartbeat(void);

#endif
