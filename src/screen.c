#include "screen.h"

static screen_line_t *line_at(screen_t *s, unsigned row) {
	return &s->lines[s->row_map[row]];
}

static void set_colours(screen_line_t *line, unsigned col, uint8_t fg, uint8_t bg) {
	unsigned shift = SCREEN_CELL_SHIFT(col);
	for (int plane = 0; plane < SCREEN_PLANES; ++plane) {
		uint32_t bits = (fg & 3u) | (bg & 3u) << 2;
		uint32_t *word = &line->colour[plane][SCREEN_CELL_WORD(col)];
		*word = (*word & ~(0xfu << shift)) | bits << shift;
		fg >>= 2;
		bg >>= 2;
	}
}

static void fill_line(screen_line_t *line, uint8_t fg, uint8_t bg) {
	for (int plane = 0; plane < SCREEN_PLANES; ++plane) {
		uint32_t bits = (fg >> 2 * plane & 3u) | (bg >> 2 * plane & 3u) << 2;
		uint32_t word = bits * 0x11111111u;
		for (int i = 0; i < SCREEN_COLOUR_WORDS; ++i)
			line->colour[plane][i] = word;
	}
	for (int col = 0; col < SCREEN_COLS; ++col)
		line->glyph[col] = ' ';
}

void screen_init(screen_t *s, uint8_t fg, uint8_t bg) {
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		s->row_map[row] = (uint8_t)row;
		fill_line(&s->lines[row], fg, bg);
	}
	s->cursor_col = s->cursor_row = 0;
	s->cursor_visible = false;
}

void screen_put(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg) {
	if (col >= SCREEN_COLS || row >= SCREEN_ROWS)
		return;
	screen_line_t *line = line_at(s, row);
	line->glyph[col] = glyph;
	set_colours(line, col, fg, bg);
}

void screen_puts(screen_t *s, unsigned col, unsigned row, const char *text, uint8_t fg, uint8_t bg) {
	for (; *text; ++text, ++col)
		screen_put(s, col, row, (uint8_t)*text, fg, bg);
}

uint8_t screen_glyph(const screen_t *s, unsigned col, unsigned row) {
	if (col >= SCREEN_COLS || row >= SCREEN_ROWS)
		return 0;
	return screen_line(s, row)->glyph[col];
}

void screen_colours(const screen_t *s, unsigned col, unsigned row, uint8_t *fg, uint8_t *bg) {
	*fg = *bg = 0;
	if (col >= SCREEN_COLS || row >= SCREEN_ROWS)
		return;
	const screen_line_t *line = screen_line(s, row);
	for (int plane = 0; plane < SCREEN_PLANES; ++plane) {
		uint32_t bits = line->colour[plane][SCREEN_CELL_WORD(col)] >> SCREEN_CELL_SHIFT(col);
		*fg |= (uint8_t)((bits & 3u) << 2 * plane);
		*bg |= (uint8_t)((bits >> 2 & 3u) << 2 * plane);
	}
}

void screen_clear_line(screen_t *s, unsigned row, uint8_t fg, uint8_t bg) {
	if (row < SCREEN_ROWS)
		fill_line(line_at(s, row), fg, bg);
}

void screen_scroll_up(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg) {
	if (top >= bottom || bottom >= SCREEN_ROWS)
		return;
	// The line leaving the top is recycled as the new bottom row.
	uint8_t recycled = s->row_map[top];
	for (unsigned row = top; row < bottom; ++row)
		s->row_map[row] = s->row_map[row + 1];
	s->row_map[bottom] = recycled;
	fill_line(&s->lines[recycled], fg, bg);
}
