#include "screen.h"

#include <string.h>

static void fill_slot(screen_t *s, unsigned slot, uint8_t fg, uint8_t bg) {
	screen_line_t *line = &s->lines[slot];
	memset(line->glyph, ' ', sizeof line->glyph);
	memset(line->fg, fg, sizeof line->fg);
	memset(line->bg, bg, sizeof line->bg);
	s->dirty |= 1u << slot;
}

void screen_init(screen_t *s, unsigned cols, uint8_t fg, uint8_t bg) {
	s->cols = cols == 132 ? 132 : 80;
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		s->row_map[row] = (uint8_t)row;
		fill_slot(s, row, fg, bg);
	}
	s->cursor_col = s->cursor_row = 0;
	s->cursor_visible = false;
}

void screen_put(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg) {
	if (col >= s->cols || row >= SCREEN_ROWS)
		return;
	unsigned slot = s->row_map[row];
	screen_line_t *line = &s->lines[slot];
	line->glyph[col] = glyph;
	line->fg[col] = fg;
	line->bg[col] = bg;
	s->dirty |= 1u << slot;
}

void screen_puts(screen_t *s, unsigned col, unsigned row, const char *text, uint8_t fg, uint8_t bg) {
	for (; *text; ++text, ++col)
		screen_put(s, col, row, (uint8_t)*text, fg, bg);
}

uint8_t screen_glyph(const screen_t *s, unsigned col, unsigned row) {
	if (col >= s->cols || row >= SCREEN_ROWS)
		return 0;
	return screen_line(s, row)->glyph[col];
}

void screen_colours(const screen_t *s, unsigned col, unsigned row, uint8_t *fg, uint8_t *bg) {
	*fg = *bg = 0;
	if (col >= s->cols || row >= SCREEN_ROWS)
		return;
	const screen_line_t *line = screen_line(s, row);
	*fg = line->fg[col];
	*bg = line->bg[col];
}

void screen_clear_line(screen_t *s, unsigned row, uint8_t fg, uint8_t bg) {
	if (row < SCREEN_ROWS)
		fill_slot(s, s->row_map[row], fg, bg);
}

void screen_scroll_up(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg) {
	if (top >= bottom || bottom >= SCREEN_ROWS)
		return;
	// The slot leaving the top is recycled as the new bottom row.
	uint8_t recycled = s->row_map[top];
	for (unsigned row = top; row < bottom; ++row)
		s->row_map[row] = s->row_map[row + 1];
	s->row_map[bottom] = recycled;
	fill_slot(s, recycled, fg, bg);
}
