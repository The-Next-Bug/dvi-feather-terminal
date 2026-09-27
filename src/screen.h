#ifndef SCREEN_H
#define SCREEN_H

// The character cell buffer: what the terminal writes (core 0) and the video
// code renders. Portable C with no pico-sdk dependency, so it is unit-tested
// on the host.
//
// Each cell holds a glyph code (see font.h) and RGB222 (0bRRGGBB) foreground
// and background colours. The screen is 80 or 132 columns wide.
//
// Display rows map to line slots through row_map, so scrolling rotates
// indices instead of copying cells. Changed slots are flagged in `dirty`
// for the renderer, which keeps its pixels per slot too, so a scroll only
// re-renders the recycled line.
//
// The cursor is not drawn into the cells: the renderer shows it by
// inverting the cursor cell's pixels.

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_MAX_COLS 132
#define SCREEN_ROWS 25 // 24 text rows + a status line

#define RGB222(r, g, b) ((uint8_t)(((r) & 3) << 4 | ((g) & 3) << 2 | ((b) & 3)))

typedef struct {
	uint8_t glyph[SCREEN_MAX_COLS];
	uint8_t fg[SCREEN_MAX_COLS];
	uint8_t bg[SCREEN_MAX_COLS];
} screen_line_t;

typedef struct {
	screen_line_t lines[SCREEN_ROWS];
	uint8_t row_map[SCREEN_ROWS];
	uint8_t cols;
	uint8_t cursor_col, cursor_row;
	bool cursor_visible;
	uint32_t dirty; // bit n: lines[n] changed since the renderer last saw it
} screen_t;

// Set the width (80 or 132; anything else is taken as 80), map every row to
// its own slot, fill the screen with blanks in fg/bg and hide the cursor.
// Every slot is marked dirty.
void screen_init(screen_t *s, unsigned cols, uint8_t fg, uint8_t bg);

// Out-of-range cells are ignored.
void screen_put(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg);
void screen_puts(screen_t *s, unsigned col, unsigned row, const char *text, uint8_t fg, uint8_t bg);

uint8_t screen_glyph(const screen_t *s, unsigned col, unsigned row);
void screen_colours(const screen_t *s, unsigned col, unsigned row, uint8_t *fg, uint8_t *bg);

void screen_clear_line(screen_t *s, unsigned row, uint8_t fg, uint8_t bg);

// Scroll rows top..bottom (inclusive) up by one; the bottom row is cleared.
void screen_scroll_up(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg);

static inline const screen_line_t *screen_line(const screen_t *s, unsigned row) {
	return &s->lines[s->row_map[row]];
}

#endif
