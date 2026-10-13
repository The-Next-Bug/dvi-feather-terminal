#ifndef DRIVEPANEL_H
#define DRIVEPANEL_H

// The Drives panel of SET-UP in CP/M mode: what is mounted on drives A-H,
// and a way to change it while CP/M runs. Portable C (FatFs, drives.h),
// host-tested; the terminal (apps/terminal/setup_glue.c) opens it from
// SET-UP (D) over the same rows.
//
// Up/Down select a drive, Left/Right or Space step through the disk images
// on the card (and "none"), Return mounts the choices and saves them,
// Escape goes back without changes. CP/M 2.2 only sees a changed drive
// after a warm boot (^C at the prompt), as with a real floppy swap; CP/M 3
// at once.
//
// Named drive sets (bd f8w.6, drives.h): the Set row above A: steps through
// the sets on the card (Left/Right/Space), each filling in the drives
// (Return mounts them as above), and shows the set the drives match. S
// saves the drives as a set (its name typed in), R or Delete removes the
// set shown (after Y). Saving and removing touch the card, so they come
// back as actions for the caller to run (drivepanel_save_set, _remove_set).

#include <stdbool.h>
#include <stdint.h>

#include "drives.h"
#include "screen.h"

#define DRIVEPANEL_ROWS (DRIVES + 7) // in a box: the title line, the Set row, the drives, message, 2 help lines, the bottom line
#define DRIVEPANEL_ESC_TIMEOUT_MS 100

typedef enum {
	DRIVEPANEL_NONE,
	DRIVEPANEL_REDRAW,
	DRIVEPANEL_APPLY,  // Return: drivepanel_apply, then close
	DRIVEPANEL_CANCEL, // Escape
	DRIVEPANEL_SAVE_SET,   // drivepanel_save_set
	DRIVEPANEL_REMOVE_SET, // drivepanel_remove_set
} drivepanel_action_t;

typedef enum {
	DRIVEPANEL_PROMPT_NONE,
	DRIVEPANEL_PROMPT_NAME,   // typing the name to save the drives as
	DRIVEPANEL_PROMPT_REMOVE, // Y removes the set shown
} drivepanel_prompt_t;

typedef struct {
	char images[DRIVES_LIST_MAX][DRIVES_NAME_MAX + 1]; // on the card, sorted
	unsigned n_images;
	int choice[DRIVES]; // index into images, -1: none
	int mounted[DRIVES]; // what was mounted when opened, as an index
	unsigned drive;      // selected
	// The sets on the card, each as indices into images (-1: none), and
	// how many of its images are not on the card (left as none).
	char sets[DRIVES_SETS_MAX][DRIVES_NAME_MAX + 1];
	int8_t set_choice[DRIVES_SETS_MAX][DRIVES];
	uint8_t set_missing[DRIVES_SETS_MAX];
	unsigned n_sets;
	bool on_set; // the Set row is selected, not a drive
	int set;     // the set shown: the one the choices match, else -1
	drivepanel_prompt_t prompt;
	char name[DRIVES_NAME_MAX + 1]; // the name being typed
	uint8_t esc;         // 0, 1 after ESC, 2 inside ESC [ / ESC O
	uint8_t esc_param;   // the CSI's first parameter digit (3: Delete)
	uint32_t esc_ms;
	const char *message;
	char message_buf[64];
} drivepanel_t;

// Read the card's images and the drives' assignments.
void drivepanel_open(drivepanel_t *p);

drivepanel_action_t drivepanel_input(drivepanel_t *p, uint8_t c, uint32_t now_ms);
drivepanel_action_t drivepanel_tick(drivepanel_t *p, uint32_t now_ms);

// Mount the changed drives and save the assignments. Returns the number of
// drives changed; *failed is set if one could not be mounted.
unsigned drivepanel_apply(drivepanel_t *p, bool *failed);

// Save the drives as set p->name, or remove the set shown; the sets are
// read again, the drives kept as chosen, and p->message says how it went.
void drivepanel_save_set(drivepanel_t *p);
void drivepanel_remove_set(drivepanel_t *p);

// Draw it like SET-UP (setup_draw): a box in border, text in fg on bg.
void drivepanel_draw(const drivepanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border);

#endif
