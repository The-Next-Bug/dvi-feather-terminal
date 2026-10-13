#ifndef FWPANEL_H
#define FWPANEL_H

// SET-UP's firmware update panel (U, in either mode): install
// /FIRMWARE.UF2 from the SD card or the USB stick (FatFs volumes 0 and 1).
// Portable C (FatFs), host-tested; the terminal
// (apps/terminal/setup_glue.c) opens it over SET-UP's rows, feeds it keys
// and lets it do the work from its main loop (fwpanel_run), never from
// inside the USB stack.
//
// Opened, it looks for the file on both (with it on both, S or U picks
// one), checks it (fwupdate_check) and asks; Y writes it to the
// staging area with a progress bar, renames it to FIRMWARE.OLD, closes
// everything on the card and installs it (fwupdate_commit restarts the
// board). N or Escape goes back to SET-UP, as does any key after a failure.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"

// In the root of the card or stick; renamed to FWPANEL_FILE_OLD once
// staged, so it is not installed twice by mistake. 8.3 names: FatFs has no
// long names here. Without a drive number: the SD card (volume 0).
#define FWPANEL_FILE "/FIRMWARE.UF2"
#define FWPANEL_FILE_OLD "/FIRMWARE.OLD"

#define FWPANEL_ROWS 8 // in a box: title line, blank, 3 text rows, blank, help, bottom line
#define FWPANEL_INFO 96

// Where the file is: FatFs volume numbers.
typedef enum {
	FWPANEL_SD,
	FWPANEL_USB,
	FWPANEL_SOURCES,
} fwpanel_source_t;

typedef enum {
	FWPANEL_SEARCH,  // the search (and check) is due (fwpanel_run)
	FWPANEL_PICK,    // on both: S or U
	FWPANEL_CHECK,   // the check of p->source's file is due (fwpanel_run)
	FWPANEL_ASK,     // Y or N
	FWPANEL_STAGE,   // Y: staging is due (fwpanel_run)
	FWPANEL_FAILED,  // a key goes back
} fwpanel_state_t;

typedef enum {
	FWPANEL_NONE,
	FWPANEL_REDRAW,
	FWPANEL_WORK,   // fwpanel_run has something to do
	FWPANEL_CANCEL, // back to SET-UP
} fwpanel_action_t;

typedef struct fwpanel fwpanel_t;

// What the panel needs from the board (fwupdate.h on the board, fakes on
// the host).
typedef struct {
	// Make the card's file system available (false: why in info).
	bool (*mount)(char *info, size_t n);
	// Whether a USB stick is mounted as volume 1 (NULL: no USB host).
	bool (*stick)(void);
	bool (*check)(const char *path, char *info, size_t n);
	bool (*stage)(const char *path, void (*progress)(void *ctx, uint32_t done, uint32_t total), void *ctx,
		char *info, size_t n);
	// Done with the card: after a cancel or failure (commit false), or
	// before the commit (true: flush and close everything on it and on
	// the stick; called whether or not the card was mounted).
	void (*unmount)(bool commit);
	void (*commit)(void); // does not return on the board
	void (*show)(const fwpanel_t *p); // draw the panel now (during the long steps)
} fwpanel_platform_t;

struct fwpanel {
	fwpanel_state_t state;
	char info[FWPANEL_INFO]; // the file, or what went wrong
	const char *step;        // what is going on, NULL: nothing
	uint32_t done, total;    // staging progress, in bytes of the file
	bool mounted;            // the card, by plat->mount
	fwpanel_source_t source; // the file being checked or installed
};

void fwpanel_open(fwpanel_t *p);

// One key byte (an Escape sequence's ESC alone cancels: there is nothing
// else to do with arrows here).
fwpanel_action_t fwpanel_input(fwpanel_t *p, uint8_t c);

// Do what is due (FWPANEL_SEARCH, FWPANEL_CHECK, FWPANEL_STAGE): returns FWPANEL_REDRAW.
// Staging ends in the commit, which on the board does not return.
fwpanel_action_t fwpanel_run(fwpanel_t *p, const fwpanel_platform_t *plat);

// Leaving the panel: the card is let go (plat->unmount(false)) if mounted.
void fwpanel_close(fwpanel_t *p, const fwpanel_platform_t *plat);

void fwpanel_draw(const fwpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border);

// The file's path on a volume.
const char *fwpanel_path(fwpanel_source_t source);

// Rename the installed file to FIRMWARE.OLD on its volume (replacing an
// older one). False if it could not be renamed.
bool fwpanel_retire_file(fwpanel_source_t source);

#endif
