#ifndef ADM3A_H
#define ADM3A_H

// The ADM-3A personality of the terminal (term.h), for term.c only.
// Portable C, host-tested through test_term.
//
// Lear Siegler ADM-3A as terminfo's adm3a: ^H left, ^J down (scrolling at
// the bottom), ^K up, ^L right (none of them erase; all stop at the
// edges), ^M return, ^Z clear and home, ^^ home, ESC = row+32 col+32
// cursor address; printing wraps at once in the last column (no deferred
// wrap), scrolling from the bottom right. Plus the Kaypro II's additions
// (terminfo kaypro), which ADM-3A software never sends: ^X clear to end
// of line, ^W clear to end of screen, ESC E insert line, ESC R delete line.
// No attributes, no character sets, no replies to the host; 80 columns.

#include <stdint.h>

#include "term.h"

void adm3a_reset(term_t *t); // also abandons an ESC = in progress
void adm3a_byte(term_t *t, uint8_t ch);

#endif
