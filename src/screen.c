#include "screen.h"

#include <string.h>

static void fill_slot(screen_t *s, unsigned slot, uint8_t fg, uint8_t bg) {
	screen_line_t *line = &s->lines[slot];
	memset(line->glyph, ' ', sizeof line->glyph);
	memset(line->fg, fg, sizeof line->fg);
	memset(line->bg, bg, sizeof line->bg);
	memset(line->attr, 0, sizeof line->attr);
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
	s->reverse_rows = 0;
}

void screen_put_attr(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg,
		uint8_t attr) {
	if (col >= s->cols || row >= SCREEN_ROWS)
		return;
	unsigned slot = s->row_map[row];
	screen_line_t *line = &s->lines[slot];
	line->glyph[col] = glyph;
	line->fg[col] = fg;
	line->bg[col] = bg;
	line->attr[col] = attr;
	s->dirty |= 1u << slot;
}

void screen_put(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg) {
	screen_put_attr(s, col, row, glyph, fg, bg, 0);
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

uint8_t screen_attr(const screen_t *s, unsigned col, unsigned row) {
	if (col >= s->cols || row >= SCREEN_ROWS)
		return 0;
	return screen_line(s, row)->attr[col];
}

void screen_set_reverse_rows(screen_t *s, uint32_t mask) {
	uint32_t changed = s->reverse_rows ^ mask;
	s->reverse_rows = mask;
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		if (changed >> row & 1)
			s->dirty |= 1u << s->row_map[row];
}

void screen_clear_line(screen_t *s, unsigned row, uint8_t fg, uint8_t bg) {
	if (row < SCREEN_ROWS)
		fill_slot(s, s->row_map[row], fg, bg);
}

void screen_save_row(const screen_t *s, unsigned row, screen_line_t *out) {
	if (row < SCREEN_ROWS)
		*out = *screen_line(s, row);
}

void screen_restore_row(screen_t *s, unsigned row, const screen_line_t *in) {
	if (row >= SCREEN_ROWS)
		return;
	s->lines[s->row_map[row]] = *in;
	s->dirty |= 1u << s->row_map[row];
}

void screen_scroll_down(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg) {
	if (top >= bottom || bottom >= SCREEN_ROWS)
		return;
	// The slot leaving the bottom is recycled as the new top row.
	uint8_t recycled = s->row_map[bottom];
	for (unsigned row = bottom; row > top; --row)
		s->row_map[row] = s->row_map[row - 1];
	s->row_map[top] = recycled;
	fill_slot(s, recycled, fg, bg);
}

void screen_erase(screen_t *s, unsigned row, unsigned from, unsigned to, uint8_t fg, uint8_t bg) {
	if (row >= SCREEN_ROWS)
		return;
	if (to > s->cols)
		to = s->cols;
	if (from >= to)
		return;
	unsigned slot = s->row_map[row];
	screen_line_t *line = &s->lines[slot];
	memset(&line->glyph[from], ' ', to - from);
	memset(&line->fg[from], fg, to - from);
	memset(&line->bg[from], bg, to - from);
	memset(&line->attr[from], 0, to - from);
	s->dirty |= 1u << slot;
}

// Move count cells of a line from column `from` to column `to`.
static void shift_cells(screen_line_t *line, unsigned to, unsigned from, unsigned count) {
	memmove(&line->glyph[to], &line->glyph[from], count);
	memmove(&line->fg[to], &line->fg[from], count);
	memmove(&line->bg[to], &line->bg[from], count);
	memmove(&line->attr[to], &line->attr[from], count);
}

void screen_insert_cells(screen_t *s, unsigned row, unsigned col, unsigned n, uint8_t fg, uint8_t bg) {
	if (row >= SCREEN_ROWS || col >= s->cols)
		return;
	if (n > s->cols - col)
		n = s->cols - col;
	shift_cells(&s->lines[s->row_map[row]], col + n, col, s->cols - col - n);
	screen_erase(s, row, col, col + n, fg, bg);
}

void screen_delete_cells(screen_t *s, unsigned row, unsigned col, unsigned n, uint8_t fg, uint8_t bg) {
	if (row >= SCREEN_ROWS || col >= s->cols)
		return;
	if (n > s->cols - col)
		n = s->cols - col;
	shift_cells(&s->lines[s->row_map[row]], col, col + n, s->cols - col - n);
	screen_erase(s, row, s->cols - n, s->cols, fg, bg);
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
