#ifndef SCREEN_H
#define SCREEN_H

// The character cell buffer shared by the terminal (core 0, writer) and the
// video renderer (core 1, reader). Portable C with no pico-sdk dependency, so
// it is unit-tested on the host.
//
// Each line is stored in the format the renderer consumes directly: one glyph
// code per cell (see font.h), plus three colour planes (blue, green, red, as
// TMDS lanes 0-2) holding a 2-bit foreground and background per cell. Colours
// are RGB222 (0bRRGGBB).
//
// Display rows map to lines through row_map, so scrolling rotates indices
// instead of copying cells.
//
// The cursor is not drawn into the cells: the renderer shows it by swapping
// the foreground and background of the cursor cell as it draws the row.

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_COLS 80
#define SCREEN_ROWS 30
#define SCREEN_PLANES 3
#define SCREEN_CELLS_PER_WORD 8 // 4 colour bits per cell per plane
#define SCREEN_COLOUR_WORDS (SCREEN_COLS / SCREEN_CELLS_PER_WORD)

// Cell colour bits within a plane word: fg in bits 1:0, bg in bits 3:2.
#define SCREEN_CELL_SHIFT(col) ((col) % SCREEN_CELLS_PER_WORD * 4)
#define SCREEN_CELL_WORD(col) ((col) / SCREEN_CELLS_PER_WORD)

#define RGB222(r, g, b) ((uint8_t)(((r) & 3) << 4 | ((g) & 3) << 2 | ((b) & 3)))

typedef struct {
	uint8_t glyph[SCREEN_COLS];
	uint32_t colour[SCREEN_PLANES][SCREEN_COLOUR_WORDS];
} screen_line_t;

typedef struct {
	screen_line_t lines[SCREEN_ROWS];
	uint8_t row_map[SCREEN_ROWS];
	uint8_t cursor_col, cursor_row;
	bool cursor_visible;
} screen_t;

// Map every row to its own line, fill the screen with blanks in fg/bg and
// hide the cursor.
void screen_init(screen_t *s, uint8_t fg, uint8_t bg);

// Out-of-range cells are ignored.
void screen_put(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg);
void screen_puts(screen_t *s, unsigned col, unsigned row, const char *text, uint8_t fg, uint8_t bg);

uint8_t screen_glyph(const screen_t *s, unsigned col, unsigned row);
void screen_colours(const screen_t *s, unsigned col, unsigned row, uint8_t *fg, uint8_t *bg);

void screen_clear_line(screen_t *s, unsigned row, uint8_t fg, uint8_t bg);

// Scroll rows top..bottom (inclusive) up by one; the bottom row is cleared.
void screen_scroll_up(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg);

// Return a plane word with the fg and bg of the cell at col swapped.
static inline uint32_t screen_swap_cell_colours(uint32_t word, unsigned col) {
	unsigned shift = SCREEN_CELL_SHIFT(col);
	uint32_t cell = word >> shift & 0xfu;
	uint32_t swapped = (cell >> 2 | cell << 2) & 0xfu;
	return (word & ~(0xfu << shift)) | swapped << shift;
}

static inline const screen_line_t *screen_line(const screen_t *s, unsigned row) {
	return &s->lines[s->row_map[row]];
}

#endif
