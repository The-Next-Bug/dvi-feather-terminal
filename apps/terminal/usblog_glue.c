// The USB log on the SD card (bd xv0): usbkbd.c's events and TinyUSB's
// error messages, appended to USBLOG.TXT in the card's root (created if
// missing) every few seconds while SET-UP's USB log setting is on, as
// saved. From the main loop only, never inside FatFs or the USB stack.
// Cold code in flash (ld/).

#include <stdio.h>
#include <string.h>

#include "datetime.h"
#include "ff.h"
#include "rtc.h"
#include "terminal.h"
#include "term_build.h"
#include "usbkbd.h"

#define LOG_FILE "0:/USBLOG.TXT"
#define WRITE_EVERY_MS 2000
// In terminal mode nothing else mounts the card: try again this seldom.
#define MOUNT_EVERY_MS 60000

static uint32_t now_ms(void) {
	return to_ms_since_boot(get_absolute_time());
}

static bool card_mounted(uint32_t now) {
	static uint32_t next_mount;
	static bool tried;
	FATFS *fs = monitor_card_fs();
	if (fs->fs_type) {
		return true;
	}
	// In CP/M mode the monitor mounts the card (and says why it could not).
	if (cpm.on || (tried && (int32_t)(now - next_mount) < 0)) {
		return false;
	}
	tried = true;
	next_mount = now + MOUNT_EVERY_MS;
	return f_mount(fs, "", 1) == FR_OK;
}

static void put(FIL *f, const char *s) {
	UINT done;
	f_write(f, s, (UINT)strlen(s), &done);
}

// The first lines of a boot's log: which firmware, and when.
static void put_header(FIL *f, uint32_t now) {
	char line[96];
	put(f, "\r\n--- " TERM_BUILD_ID " (" TERM_BUILD_COMMIT ") started");
	if (rtc_valid()) {
		const datetime_clock_t iso = {.seconds = true, .date = DATETIME_DATE_ISO};
		char clock[DATETIME_CLOCK_MAX + 1];
		datetime_clock_text(clock, rtc_now() - now / 1000, true, &iso);
		snprintf(line, sizeof line, " at %s", clock);
		put(f, line);
	}
	put(f, "; times are seconds since then\r\n");
}

void usblog_poll(void) {
	static uint32_t next_write;
	static bool header_written;
	static FIL file;
	uint32_t now = now_ms();
	if ((int32_t)(now - next_write) < 0) {
		return;
	}
	next_write = now + WRITE_EVERY_MS;
	size_t n;
	bool lost;
	const char *text = usbkbd_log_pending(&n, &lost);
	if (!n) {
		return;
	}
	if (settings.usb_log != SETTINGS_USB_LOG_SD) {
		usbkbd_log_done(n);
		return;
	}
	// The card not mounted yet (CP/M mode: until the monitor starts): the
	// text waits (what does not fit is noted as lost).
	if (!card_mounted(now)) {
		return;
	}
	if (f_open(&file, LOG_FILE, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
		if (!header_written) {
			put_header(&file, now);
			header_written = true;
		}
		if (lost) {
			put(&file, "(some lines did not fit in the log's buffer)\r\n");
		}
		UINT done;
		f_write(&file, text, (UINT)n, &done);
		f_close(&file);
	}
	usbkbd_log_done(n);
}
