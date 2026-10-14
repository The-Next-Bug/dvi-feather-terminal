#ifndef SCREEN_H
#define SCREEN_H

// The character cell buffer: what the terminal writes (core 0) and the video
// code renders. Portable C with no pico-sdk dependency, so it is unit-tested
// on the host.
//
// Each cell holds a glyph code (see font.h), foreground and background
// colours as palette indices (palette.h: what they look like is decided at
// display time), and attribute bits (underline, blink, bold). The screen
// is 80 or 132 columns wide. Rows in reverse_rows are shown with every
// cell's fg and bg swapped (DECSCNM).
//
// Display rows map to line slots through row_map, so scrolling rotates
// indices instead of copying cells. Changed slots are flagged in `dirty`
// for the renderer, which keeps its pixels per slot too, so a scroll only
// re-renders the recycled line.
//
// The cursor is not drawn into the cells: the renderer shows it by
// inverting the cursor cell's pixels.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_MAX_COLS 132
#define SCREEN_ROWS 25 // 24 text rows + a status line

// A 6-bit colour 0bRRGGBB (the logo's tint and the HSTX test pattern).
#define RGB222(r, g, b) ((uint8_t)(((r) & 3) << 4 | ((g) & 3) << 2 | ((b) & 3)))

// Cell attribute bits.
#define SCREEN_ATTR_UNDERLINE 0x01
#define SCREEN_ATTR_BLINK 0x02
#define SCREEN_ATTR_BOLD 0x04 // drawn emboldened (the colour is already bright)

typedef struct {
	uint8_t glyph[SCREEN_MAX_COLS];
	uint8_t fg[SCREEN_MAX_COLS];
	uint8_t bg[SCREEN_MAX_COLS];
	uint8_t attr[SCREEN_MAX_COLS];
} screen_line_t;

typedef struct {
	screen_line_t lines[SCREEN_ROWS];
	uint8_t row_map[SCREEN_ROWS];
	uint8_t cols;
	uint8_t cursor_col, cursor_row;
	bool cursor_visible;
	uint32_t reverse_rows; // bit n: display row n is shown reversed
	uint32_t dirty; // bit n: lines[n] changed since the renderer last saw it
	// A scroll is about to reuse slot as display row `row`, cleared to
	// fg/bg: called after the cells are cleared, before row_map changes, so
	// the renderer can blank what it shows for the slot at once (it would
	// otherwise show the line that left until it next renders the slot).
	// NULL: nothing. screen_init leaves it as it is (screen_set_recycle).
	void (*recycle)(void *ctx, unsigned slot, unsigned row, uint8_t fg, uint8_t bg);
	void *recycle_ctx;
} screen_t;

// Set the width (80 or 132; anything else is taken as 80), map every row to
// its own slot, fill the screen with blanks in fg/bg and hide the cursor.
// Every slot is marked dirty.
void screen_init(screen_t *s, unsigned cols, uint8_t fg, uint8_t bg);

// The recycle hook (screen_t.recycle).
void screen_set_recycle(screen_t *s, void (*recycle)(void *ctx, unsigned slot, unsigned row, uint8_t fg, uint8_t bg),
	void *ctx);

// Out-of-range cells are ignored. screen_put clears the cell's attributes.
void screen_put(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg);
void screen_put_attr(screen_t *s, unsigned col, unsigned row, uint8_t glyph, uint8_t fg, uint8_t bg,
	uint8_t attr);
void screen_puts(screen_t *s, unsigned col, unsigned row, const char *text, uint8_t fg, uint8_t bg);

uint8_t screen_glyph(const screen_t *s, unsigned col, unsigned row);
void screen_colours(const screen_t *s, unsigned col, unsigned row, uint8_t *fg, uint8_t *bg);
uint8_t screen_attr(const screen_t *s, unsigned col, unsigned row);

// Show the display rows in mask reversed (and the rest normal).
void screen_set_reverse_rows(screen_t *s, uint32_t mask);

void screen_clear_line(screen_t *s, unsigned row, uint8_t fg, uint8_t bg);

// A panel over rows top .. top + rows - 1 (rows >= 2), the full width: a
// single-line box in border on bg, the inside cleared to fg on bg, and
// title (NULL: none) in the top line from column 2, in fg. The inside is
// columns 1 .. cols - 2 of rows top + 1 .. top + rows - 2.
void screen_frame(screen_t *s, unsigned top, unsigned rows, const char *title, uint8_t fg, uint8_t bg,
	uint8_t border);

// Saved rows: copies of a screen's rows to put back later, as entries in
// one buffer the owner provides, each tagged by its owner (the alternate
// screen keeps the main screen in it, SET-UP the rows under its panel), so
// the users share the memory. An entry keeps the width the screen had (4
// bytes a cell), so 80 columns cost less than 132. Entries can be put back
// or dropped in any order.
#define SCREEN_STASH_ENTRIES 2
#define SCREEN_STASH_BYTES(rows, cols) ((size_t)(rows) * (cols) * 4u)

typedef struct {
	uint8_t tag, top, rows, cols;
} screen_stash_entry_t;

typedef struct {
	uint8_t *buf;
	size_t size, used;
	uint8_t n;
	screen_stash_entry_t entry[SCREEN_STASH_ENTRIES]; // in buffer order
} screen_stash_t;

void screen_stash_init(screen_stash_t *st, uint8_t *buf, size_t size);

// Save rows top .. top + rows - 1 under tag (non-zero). False, saving
// nothing, when they do not fit or the tag already has an entry.
bool screen_stash_push(screen_stash_t *st, const screen_t *s, uint8_t tag, unsigned top, unsigned rows);

// Put tag's rows back where they came from and drop the entry. Cells past
// the saved width (the screen got wider) are cleared to fg/bg. False when
// tag has no entry.
bool screen_stash_pop(screen_stash_t *st, screen_t *s, uint8_t tag, uint8_t fg, uint8_t bg);

// Forget tag's entry, if any.
void screen_stash_drop(screen_stash_t *st, uint8_t tag);

bool screen_stash_holds(const screen_stash_t *st, uint8_t tag);

// Scroll rows top..bottom (inclusive) up by one; the bottom row is cleared.
void screen_scroll_up(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg);

// Scroll rows top..bottom (inclusive) down by one; the top row is cleared.
void screen_scroll_down(screen_t *s, unsigned top, unsigned bottom, uint8_t fg, uint8_t bg);

// Blank cells from..to-1 of a row (clipped to the width).
void screen_erase(screen_t *s, unsigned row, unsigned from, unsigned to, uint8_t fg, uint8_t bg);

// Insert n blank cells at col, shifting the rest of the row right (cells
// pushed past the edge are lost); or delete n cells at col, shifting the
// rest left and blanking the end.
void screen_insert_cells(screen_t *s, unsigned row, unsigned col, unsigned n, uint8_t fg, uint8_t bg);
void screen_delete_cells(screen_t *s, unsigned row, unsigned col, unsigned n, uint8_t fg, uint8_t bg);

static inline const screen_line_t *screen_line(const screen_t *s, unsigned row) {
	return &s->lines[s->row_map[row]];
}

#endif
