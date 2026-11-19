// The printer on TTY2 (terminal.h, bd wt6.6): in CP/M mode, with the
// Printer setting on, the Z80's printer ports (5/6, machine.h) go to the
// print-job engine (src/printer/printjob.c), which drives a Zimodem WiFi
// modem on the terminal UART: AT+PRINT to an IPP printer, a job at a time. TTY2 is then the printer's only: SIO2 (AUX:,
// MP/M's console 1) gets no input and its output is dropped.
//
// Cold (in flash, ld/): bytes come at the line's rate at most, and nothing
// here runs on core 1, while the flash is written or in the update's commit.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "printjob.h"
#include "serial.h"
#include "terminal.h"

static printjob_t job;
static bool printer_active; // CP/M mode with the Printer setting on

static void line_put(uint8_t c) {
	serial_write(&c, 1);
}

static bool line_get(uint8_t *c) {
	return serial_read(c, 1) == 1;
}

static const printjob_io_t line_io = {
	.room = serial_tx_room,
	.put = line_put,
	.get = line_get,
	.drained = serial_tx_drained,
};

void printer_configure(const settings_t *s) {
	static bool started;
	if (!started) {
		started = true;
		// No spool yet (bd wt6.8): a failed job's bytes are dropped.
		printjob_init(&job, &line_io, NULL);
	}
	printjob_config_t cfg = {
		.kind = cpm.on ? s->printer : PRINTJOB_OFF,
		.text = s->printer_text != 0,
		.idle_s = s->printer_idle,
	};
	snprintf(cfg.target, sizeof cfg.target, "%s", s->printer_target);
	bool same = cfg.kind == job.cfg.kind && cfg.text == job.cfg.text && cfg.idle_s == job.cfg.idle_s &&
		strcmp(cfg.target, job.cfg.target) == 0;
	if (!same) {
		printjob_configure(&job, &cfg); // SET-UP's save: a job running is dropped
	}
	printer_active = cfg.kind != PRINTJOB_OFF;
}

bool printer_has_tty2(void) {
	return printer_active;
}

void printer_poll(void) {
	if (printer_active && !modem_glue_busy()) { // the job waits for the modem's start-up
		printjob_poll(&job, time_us_64());
	}
}

void printer_out(uint8_t c) {
	if (printer_active) {
		printjob_put(&job, c, time_us_64());
	}
}

bool printer_ready(void) {
	if (!printer_active) {
		return true; // nowhere: as fast as the Z80 likes
	}
	printer_poll(); // a BIOS waiting on port 5 keeps the job going
	return printjob_ready(&job);
}

void printer_end(void) {
	printjob_end(&job);
}

printer_status_t printer_status(void) {
	if (!printer_active) {
		return PRINTER_STATUS_OFF;
	}
	if (job.state == PRINTJOB_FAILED) {
		return PRINTER_STATUS_FAILED;
	}
	if (job.state != PRINTJOB_IDLE) {
		return PRINTER_STATUS_BUSY;
	}
	return job.failed && job.error != PRINTJOB_ERR_NONE ? PRINTER_STATUS_FAILED : PRINTER_STATUS_IDLE;
}

void printer_last_job(char *out, size_t size) {
	if (!job.jobs) {
		snprintf(out, size, "none since power-on");
		return;
	}
	char now[32];
	if (job.state == PRINTJOB_OPENING) {
		snprintf(now, sizeof now, "opening");
	} else if (job.state == PRINTJOB_PRINTING || job.state == PRINTJOB_CLOSING) {
		snprintf(now, sizeof now, "printing");
	} else if (job.state == PRINTJOB_FAILED || job.error != PRINTJOB_ERR_NONE) {
		snprintf(now, sizeof now, "failed: %s", printjob_error_name(job.error));
	} else {
		snprintf(now, sizeof now, "OK");
	}
	snprintf(out, size, "%s (%lu jobs, %lu failed, %lu bytes sent)", now, (unsigned long)job.jobs,
		(unsigned long)job.failed, (unsigned long)job.bytes);
}

// HWINFO's record (port 12, HWINFO_PRINTER): what the setting makes TTY2.
size_t printer_record(uint8_t *buf) {
	char detail[HWINFO_MAX], last[64];
	if (!printer_active) {
		return hwinfo_record(buf, HWINFO_ST_ABSENT, "Printer", "off: TTY2 is the serial port (SET-UP P)");
	}
	printer_last_job(last, sizeof last);
	int n = snprintf(detail, sizeof detail, "%s %s via Zimodem on TTY2; last job %s",
		job.cfg.text ? "IPP text to" : "IPP raw to",
		job.cfg.target, last);
	if (n >= (int)sizeof detail) {
		detail[sizeof detail - 2] = '+'; // cut (a long target): say so
	}
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Printer", detail);
}
