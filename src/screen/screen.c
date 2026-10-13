#include "screen.h"

#include "font.h"

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

void screen_set_recycle(screen_t *s, void (*recycle)(void *ctx, unsigned slot, unsigned row, uint8_t fg, uint8_t bg),
		void *ctx) {
	s->recycle = recycle;
	s->recycle_ctx = ctx;
}

// Clear the slot leaving one end of a scroll and tell the renderer, before
// it is attached at the other end as row.
static void recycle_slot(screen_t *s, unsigned slot, unsigned row, uint8_t fg, uint8_t bg) {
	fill_slot(s, slot, fg, bg);
	if (s->recycle)
		s->recycle(s->recycle_ctx, slot, row, fg, bg);
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

void screen_frame(screen_t *s, unsigned top, unsigned rows, const char *title, uint8_t fg, uint8_t bg,
	uint8_t border) {
	const unsigned right = s->cols - 1u, bottom = top + rows - 1;
	for (unsigned r = top; r <= bottom; ++r) {
		screen_clear_line(s, r, fg, bg);
		bool edge = r == top || r == bottom;
		for (unsigned c = 1; edge && c < right; ++c)
			screen_put(s, c, r, GLYPH_DEC_GRAPHICS('q'), border, bg); // ─
		uint8_t left_glyph = r == top ? GLYPH_DEC_GRAPHICS('l') : r == bottom ? GLYPH_DEC_GRAPHICS('m') // ┌ └
			: GLYPH_DEC_GRAPHICS('x');                                                              // │
		uint8_t right_glyph = r == top ? GLYPH_DEC_GRAPHICS('k') : r == bottom ? GLYPH_DEC_GRAPHICS('j') // ┐ ┘
			: GLYPH_DEC_GRAPHICS('x');
		screen_put(s, 0, r, left_glyph, border, bg);
		screen_put(s, right, r, right_glyph, border, bg);
	}
	if (title) {
		// Spaced from the line, and cut short of the corner.
		unsigned c = 2;
		screen_put(s, c++, top, ' ', fg, bg);
		for (; *title && c + 1 < right; ++title)
			screen_put(s, c++, top, (uint8_t)*title, fg, bg);
		screen_put(s, c, top, ' ', fg, bg);
	}
}

// ---------------------------------------------------------------------------
// Saved rows

void screen_stash_init(screen_stash_t *st, uint8_t *buf, size_t size) {
	*st = (screen_stash_t){.buf = buf, .size = size};
}

static int stash_find(const screen_stash_t *st, uint8_t tag) {
	for (unsigned i = 0; i < st->n; ++i)
		if (st->entry[i].tag == tag)
			return (int)i;
	return -1;
}

static size_t stash_offset(const screen_stash_t *st, unsigned i) {
	size_t off = 0;
	for (unsigned j = 0; j < i; ++j)
		off += SCREEN_STASH_BYTES(st->entry[j].rows, st->entry[j].cols);
	return off;
}

bool screen_stash_holds(const screen_stash_t *st, uint8_t tag) {
	return stash_find(st, tag) >= 0;
}

bool screen_stash_push(screen_stash_t *st, const screen_t *s, uint8_t tag, unsigned top, unsigned rows) {
	size_t bytes = SCREEN_STASH_BYTES(rows, s->cols);
	if (!tag || st->n == SCREEN_STASH_ENTRIES || stash_find(st, tag) >= 0 || top + rows > SCREEN_ROWS ||
		bytes > st->size - st->used)
		return false;
	uint8_t *p = st->buf + st->used;
	for (unsigned r = 0; r < rows; ++r) {
		const screen_line_t *line = screen_line(s, top + r);
		memcpy(p, line->glyph, s->cols);
		memcpy(p + s->cols, line->fg, s->cols);
		memcpy(p + 2 * s->cols, line->bg, s->cols);
		memcpy(p + 3 * s->cols, line->attr, s->cols);
		p += 4 * s->cols;
	}
	st->entry[st->n++] = (screen_stash_entry_t){.tag = tag, .top = (uint8_t)top, .rows = (uint8_t)rows,
		.cols = s->cols};
	st->used += bytes;
	return true;
}

void screen_stash_drop(screen_stash_t *st, uint8_t tag) {
	int i = stash_find(st, tag);
	if (i < 0)
		return;
	size_t off = stash_offset(st, (unsigned)i);
	size_t bytes = SCREEN_STASH_BYTES(st->entry[i].rows, st->entry[i].cols);
	memmove(st->buf + off, st->buf + off + bytes, st->used - off - bytes);
	st->used -= bytes;
	memmove(&st->entry[i], &st->entry[i + 1], (st->n - (unsigned)i - 1) * sizeof st->entry[0]);
	--st->n;
}

bool screen_stash_pop(screen_stash_t *st, screen_t *s, uint8_t tag, uint8_t fg, uint8_t bg) {
	int i = stash_find(st, tag);
	if (i < 0)
		return false;
	const uint8_t *p = st->buf + stash_offset(st, (unsigned)i);
	unsigned cols = st->entry[i].cols, n = cols < s->cols ? cols : s->cols;
	for (unsigned r = 0; r < st->entry[i].rows; ++r, p += 4 * cols) {
		unsigned slot = s->row_map[st->entry[i].top + r];
		fill_slot(s, slot, fg, bg);
		screen_line_t *line = &s->lines[slot];
		memcpy(line->glyph, p, n);
		memcpy(line->fg, p + cols, n);
		memcpy(line->bg, p + 2 * cols, n);
		memcpy(line->attr, p + 3 * cols, n);
	}
	screen_stash_drop(st, tag);
	return true;
}

void screen_scroll_down(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg) {
	if (top >= bottom || bottom >= SCREEN_ROWS)
		return;
	// The slot leaving the bottom is recycled as the new top row: cleared
	// first, so it never shows its old line there.
	uint8_t recycled = s->row_map[bottom];
	recycle_slot(s, recycled, top, fg, bg);
	for (unsigned row = bottom; row > top; --row)
		s->row_map[row] = s->row_map[row - 1];
	s->row_map[top] = recycled;
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
	// The slot leaving the top is recycled as the new bottom row: cleared
	// first, so it never shows its old line there.
	uint8_t recycled = s->row_map[top];
	recycle_slot(s, recycled, bottom, fg, bg);
	for (unsigned row = top; row < bottom; ++row)
		s->row_map[row] = s->row_map[row + 1];
	s->row_map[bottom] = recycled;
}
