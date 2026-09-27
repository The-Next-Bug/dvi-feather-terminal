#ifndef DEMO_H
#define DEMO_H

// Colour and layout test screen for 80 and 132 columns. Portable C.

#include "screen.h"

// Re-initialise the screen at `cols` (80 or 132) and draw the demo; `status`
// goes on the last row.
void demo_draw(screen_t *s, unsigned cols, const char *status);

#endif
