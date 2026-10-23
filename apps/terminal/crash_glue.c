// Core 0 crash and hang reports (bd xv0). There is no stdio: without this
// a panic(), a hard fault or a stuck interrupt handler just stops core 0,
// with the picture left as it was (core 1 keeps the video going).
// - panic() (every caller, TinyUSB's too: --wrap=panic) and a hard fault
//   show what happened on the status line;
// - a timer alarm, core 0's NMI (EPPB NMI_MASK: it gets through with
//   interrupts disabled, e.g. a spin inside TinyUSB's critical section),
//   checks that the main loop still runs (crash_heartbeat); once it has
//   not for HANG_MS, it shows where core 0 is (the interrupted PC, LR and
//   exception) and the USB controller's state, every second, and once
//   things move again has the status line redrawn;
// - a panic, a hard fault, or core 0 stuck inside an interrupt handler for
//   REBOOT_MS reboots the board, keeping the report and the USB log text
//   not yet written (SRAM the boot does not clear): the next start shows
//   the report and puts both in USBLOG.TXT (crash_init). A stall outside
//   interrupt handlers is only shown: a long card format looks the same.
// In SRAM: none of it may run from flash.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/irq.h"
#include "hardware/structs/m33_eppb.h"
#include "hardware/structs/usb.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "pico/platform.h"

#include "nvflash.h"
#include "terminal.h"
#include "usbkbd.h"
#include "video.h"

#define TICK_US 250000
#define HANG_MS 8000
#define REBOOT_MS 20000
#define REPORT_MAGIC 0x43525348u // "CRSH"

static volatile uint32_t heartbeat;
static volatile bool redraw_status; // a hang report is on the status line, things move again
static uint alarm_num;
static uint32_t seen_beat, still_ms, in_handler_ms, shown_ms;

// The report, kept across the reboot.
typedef struct {
	uint32_t magic, check;
	char text[SCREEN_MAX_COLS + 1];
} report_t;
static report_t __uninitialized_ram(last);

static uint32_t text_check(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; ++s) {
		h = (h ^ (uint8_t)*s) * 16777619u;
	}
	return h;
}

static void keep_report(const char *text) {
	snprintf(last.text, sizeof last.text, "%s", text);
	last.check = text_check(last.text);
	last.magic = REPORT_MAGIC;
}

// On the status line, whatever core 0 was doing (it may never come back).
static void show(const char *text) {
	if (!screen.cols) {
		return; // before screen_init
	}
	char line[SCREEN_MAX_COLS + 1];
	snprintf(line, sizeof line, "%-*s", (int)screen.cols, text);
	line[screen.cols] = '\0';
	screen_puts(&screen, 0, TERM_ROWS, line, colors.status_fg, colors.status_bg);
	video_update(&screen);
}

// Show the report for a while, then reboot (never returns).
static void __attribute__((noreturn)) report_and_reboot(const char *text) {
	keep_report(text);
	show(text);
	uint32_t start = time_us_32();
	while (time_us_32() - start < REBOOT_MS * 1000u) {
		tight_loop_contents();
	}
	watchdog_reboot(0, 0, 0);
	while (true) {
		tight_loop_contents();
	}
}

void __attribute__((noreturn)) __wrap_panic(const char *format, ...) {
	char text[SCREEN_MAX_COLS + 1], what[64];
	va_list ap;
	va_start(ap, format);
	vsnprintf(what, sizeof what, format ? format : "", ap);
	va_end(ap);
	for (char *p = what; *p; ++p) {
		if (*p == '\n' || *p == '\r') {
			*p = ' ';
		}
	}
	snprintf(text, sizeof text, "PANIC from %08lx: %s",
		(unsigned long)__builtin_return_address(0), what);
	report_and_reboot(text);
}

// frame: the exception frame (r0-r3, r12, lr, pc, xpsr) of the code the
// fault (or the timer) interrupted.
static void __attribute__((used, noreturn)) hardfault_c(const uint32_t *frame) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, "HARD FAULT core %u pc %08lx lr %08lx exc %lu cfsr %08lx",
		get_core_num(), (unsigned long)frame[6], (unsigned long)frame[5], (unsigned long)(frame[7] & 0x1ff),
		(unsigned long)*(volatile uint32_t *)0xe000ed28);
	if (get_core_num() != 0) {
		keep_report(text); // core 0 still draws; the report shows at the next start
		while (true) {
			tight_loop_contents();
		}
	}
	report_and_reboot(text);
}

void __attribute__((naked)) isr_hardfault(void) {
	__asm volatile(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"b hardfault_c\n");
}

static void __attribute__((used)) tick_c(const uint32_t *frame) {
	timer_hw->intr = 1u << alarm_num;
	timer_hw->alarm[alarm_num] = timer_hw->timerawl + TICK_US;
	if (nvflash_busy) {
		return; // XIP is off: drawing may read flash (and the flash work is no hang)
	}
	uint32_t beat = heartbeat;
	if (beat != seen_beat) {
		seen_beat = beat;
		if (still_ms >= HANG_MS) {
			redraw_status = true;
			last.magic = 0; // it went on after all: not how this run ended
		}
		still_ms = in_handler_ms = shown_ms = 0;
		return;
	}
	still_ms += TICK_US / 1000;
	uint32_t exc = frame[7] & 0x1ff;
	in_handler_ms = exc ? in_handler_ms + TICK_US / 1000 : 0;
	if (still_ms < HANG_MS || still_ms - shown_ms < 1000) {
		return;
	}
	shown_ms = still_ms;
	// The USB controller: INTS, SIE_CTRL, SIE_STATUS, BUFF_STATUS.
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, "STUCK %lus pc %08lx lr %08lx x%lu i %05lx c %08lx s %08lx b %04lx",
		(unsigned long)(still_ms / 1000), (unsigned long)frame[6], (unsigned long)frame[5], (unsigned long)exc,
		(unsigned long)usb_hw->ints, (unsigned long)usb_hw->sie_ctrl, (unsigned long)usb_hw->sie_status,
		(unsigned long)usb_hw->buf_status);
	if (in_handler_ms >= REBOOT_MS) {
		report_and_reboot(text);
	}
	keep_report(text);
	show(text);
}

void __attribute__((naked)) isr_nmi(void) {
	__asm volatile(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"b tick_c\n");
}

void crash_heartbeat(void) {
	++heartbeat;
}

bool crash_redraw_status(void) {
	bool r = redraw_status;
	redraw_status = false;
	return r;
}

const char *crash_init(void) {
	static char previous[SCREEN_MAX_COLS + 1];
	bool kept = last.magic == REPORT_MAGIC && memchr(last.text, '\0', sizeof last.text) &&
		last.check == text_check(last.text);
	last.magic = 0;
	usbkbd_log_restore(kept);
	if (kept) {
		snprintf(previous, sizeof previous, "%s", last.text);
		usbkbd_log_line("--- the lines above are the previous run's last; it ended with:");
		usbkbd_log_line(previous);
		usbkbd_log("crash", 0);
	}
	// The alarm's interrupt is core 0's NMI, not enabled in the NVIC.
	alarm_num = (uint)hardware_alarm_claim_unused(true);
	uint irq = hardware_alarm_get_irq_num(alarm_num); // TIMER0_IRQ_n: below 32
	timer_hw->alarm[alarm_num] = timer_hw->timerawl + TICK_US;
	hw_set_bits(&timer_hw->inte, 1u << alarm_num);
	eppb_hw->nmi_mask[0] |= 1u << irq;
	return kept ? previous : NULL;
}
