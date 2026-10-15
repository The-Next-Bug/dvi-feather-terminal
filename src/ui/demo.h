#ifndef DEMO_H
#define DEMO_H

// Color and layout test screen for 80 and 132 columns. Portable C.

#include "screen.h"

// Re-initialize the screen at `cols` (80 or 132) and draw the demo, naming
// `font` as the one shown; `status` goes on the last row.
void demo_draw(screen_t *s, unsigned cols, const char *font, const char *status);

#endif
