#include "usbkbd.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/structs/usb.h"
#include "hardware/structs/usb_dpram.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "host/hcd.h"

#include "board.h"

// Attached keyboard interfaces, for the lock LEDs.
static struct {
	bool used;
	uint8_t dev_addr, instance;
} keyboards[CFG_TUH_HID];

// Devices TinyUSB has finished setting up (tuh_mount_cb: every interface's
// driver done), by address; hubs, which get no tuh_mount_cb, count once
// configured.
#define ADDRESSES (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB) // hubs take the top ones
static bool ready[ADDRESSES + 1];
// The lock LEDs changed and are still to be sent (usbkbd_poll).
static bool leds_pending;

#define LOG_SIZE 8
#define LOG_SHOWN 4
static struct {
	const char *what;
	uint8_t dev;
	uint16_t count;
	uint32_t s;
} usb_log[LOG_SIZE];
static unsigned log_next;

static kbd_t kbd;
static const kbd_modes_t no_modes;
// The modes for reports that arrive during this usbkbd_poll.
static const kbd_modes_t *poll_modes = &no_modes;
// The LED output report; must stay valid until the transfer completes.
static uint8_t led_report;

// The text log for the SD card (usbkbd_log_pending): CR LF lines, each
// stamped with the time since boot. TinyUSB's messages may come from its
// interrupt handler, so it is only touched with interrupts off. In SRAM
// the boot does not clear, so what was not written yet survives a crash's
// reboot (usbkbd_log_restore); text_magic tells it from power-on garbage.
#define TEXT_SIZE 2048
#define TEXT_MAGIC 0x55534c47u // "USLG"
static char __uninitialized_ram(text)[TEXT_SIZE];
static size_t __uninitialized_ram(text_len);
static uint32_t __uninitialized_ram(text_magic);
static bool text_lost, text_line_start = true;

// The USB host's watchdog (usbkbd_poll): a device set up for too long, or
// the probe, a GET_STATUS to the hub (or the device on the port) while
// nothing is being set up, failing or never coming back, restarts the
// host. A restart that does not help waits longer before the next.
#define STUCK_SETUP_MS 20000
#define PROBE_EVERY_MS 5000
#define PROBE_DEADLINE_MS 10000 // TinyUSB gives up on a control transfer after 5 s
#define PROBE_FAILS 3
#define BACKOFF_MAX 16
#define HEALTHY_MS 60000 // quiet and probed fine this long: back to the usual delays
static struct {
	uint32_t setup_since; // when a device started being set up (if setting_up)
	uint32_t next_probe, probe_sent, healthy_since; // (healthy_since if healthy)
	bool setting_up, healthy;
	uint8_t addr;   // probed device
	uint8_t fails;  // probes in a row that failed
	uint8_t backoff; // times the usual delays before the next restart
	volatile bool busy, done, ok;
} dog = {.backoff = 1};
static unsigned resets;
static uint32_t last_reset_ms;
static const char *reset_requested; // usbkbd_request_reset's why
static bool reset_waiting;
// A requested restart this soon after the last (times the backoff) waits:
// a stick that fails every time must not keep the keyboard away.
#define REQUEST_GAP_MS 10000
static const tusb_control_request_t probe_request = {
	.bmRequestType = 0x80, // device to host, standard, device
	.bRequest = TUSB_REQ_GET_STATUS,
	.wLength = 2,
};
static uint8_t probe_status[2] __attribute__((aligned(4)));

static uint32_t now_ms(void) {
	return to_ms_since_boot(get_absolute_time());
}

// Appends s to the text log; interrupts must be off. A message that does
// not fit is dropped whole (and noted), not cut.
static void text_put(const char *s) {
	char stamp[16];
	uint32_t ms = now_ms();
	int stamp_len = snprintf(stamp, sizeof stamp, "%lu.%03lu ", (unsigned long)(ms / 1000),
		(unsigned long)(ms % 1000));
	size_t need = strlen(s) * 2 + (size_t)stamp_len; // every \n may become \r\n, plus a stamp per line
	for (const char *p = s; *p; ++p) {
		need += *p == '\n' ? (size_t)stamp_len : 0;
	}
	if (text_len + need > sizeof text) {
		text_lost = true;
		return;
	}
	for (; *s; ++s) {
		if (*s == '\r') {
			continue;
		}
		if (text_line_start) {
			memcpy(text + text_len, stamp, (size_t)stamp_len);
			text_len += (size_t)stamp_len;
			text_line_start = false;
		}
		if (*s == '\n') {
			text[text_len++] = '\r';
			text_line_start = true;
		}
		text[text_len++] = *s;
	}
	text_magic = TEXT_MAGIC ^ (uint32_t)text_len;
}

static int text_vprintf(const char *format, va_list ap) {
	static char line[96]; // static: the 2 KB stack is shared with the interrupts
	uint32_t irq = save_and_disable_interrupts();
	int n = vsnprintf(line, sizeof line, format, ap);
	text_put(line);
	restore_interrupts(irq);
	return n;
}

static void text_printf(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	text_vprintf(format, ap);
	va_end(ap);
}

// TinyUSB's messages (CFG_TUSB_DEBUG_PRINTF, tusb_config.h): its errors and
// failed checks. Also called from its interrupt handler.
int usbkbd_tusb_printf(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	int n = text_vprintf(format, ap);
	va_end(ap);
	return n;
}

const char *usbkbd_log_pending(size_t *n, bool *lost) {
	uint32_t irq = save_and_disable_interrupts();
	*n = text_len;
	*lost = text_lost;
	restore_interrupts(irq);
	return text;
}

void usbkbd_log_done(size_t n) {
	uint32_t irq = save_and_disable_interrupts();
	if (n > text_len) {
		n = text_len;
	}
	memmove(text, text + n, text_len - n);
	text_len -= n;
	text_magic = TEXT_MAGIC ^ (uint32_t)text_len;
	text_lost = false;
	restore_interrupts(irq);
}

void usbkbd_log_restore(bool keep) {
	if (!keep || text_magic != (TEXT_MAGIC ^ (uint32_t)text_len) || text_len > TEXT_SIZE) {
		text_len = 0;
	}
	text_magic = TEXT_MAGIC ^ (uint32_t)text_len;
	text_line_start = true;
}

void usbkbd_log_line(const char *line) {
	text_printf("%s\n", line);
}

void usbkbd_log(const char *what, uint8_t dev) {
	text_printf("%s %u\n", what, dev);
	uint32_t s = to_ms_since_boot(get_absolute_time()) / 1000;
	if (log_next) {
		unsigned last = (log_next - 1) % LOG_SIZE;
		if (usb_log[last].what == what && usb_log[last].dev == dev) {
			if (usb_log[last].count < UINT16_MAX) {
				++usb_log[last].count;
			}
			usb_log[last].s = s;
			return;
		}
	}
	unsigned i = log_next++ % LOG_SIZE;
	usb_log[i].what = what;
	usb_log[i].dev = dev;
	usb_log[i].count = 1;
	usb_log[i].s = s;
}

void usbkbd_log_text(char *out, size_t size) {
	size_t n = 0;
	out[0] = '\0';
	unsigned first = log_next > LOG_SHOWN ? log_next - LOG_SHOWN : 0;
	for (unsigned k = first; k < log_next && n < size; ++k) {
		unsigned i = k % LOG_SIZE;
		int m = usb_log[i].count > 1
			? snprintf(out + n, size - n, " %lus %s%u x%u", (unsigned long)usb_log[i].s, usb_log[i].what,
				usb_log[i].dev, usb_log[i].count)
			: snprintf(out + n, size - n, " %lus %s%u", (unsigned long)usb_log[i].s, usb_log[i].what, usb_log[i].dev);
		if (m < 0) {
			break;
		}
		n += (size_t)m;
	}
}

bool usbkbd_device_ready(uint8_t dev_addr) {
	if (dev_addr > CFG_TUH_DEVICE_MAX && dev_addr <= ADDRESSES) {
		return tuh_mounted(dev_addr);
	}
	return dev_addr <= ADDRESSES && ready[dev_addr];
}

// No device part way through being set up. TinyUSB 0.18 had a single
// control transfer for the whole host: one of ours (the LEDs) in the middle
// of a device's setup made its next request fail, which left that device
// half set up and TinyUSB enumerating it for good: no other device (a USB
// stick plugged in later) was ever set up. Newer TinyUSB queues them, but
// ours still wait for quiet.
static bool usb_quiet(void) {
	for (uint8_t a = 1; a <= ADDRESSES; ++a) {
		uint16_t vid, pid;
		if (tuh_vid_pid_get(a, &vid, &pid) && !usbkbd_device_ready(a)) {
			return false;
		}
	}
	return true;
}

// The RP2350's USB controller polls the interrupt endpoints (the hub's,
// the keyboards') by itself while EPX is idle. A transfer started on EPX
// while such a poll is under way "completes" at once with nothing sent (a
// USB stick's 31-byte command block came back done, 0 bytes, in 15 us;
// the stick then stuck until replugged; bd xv0). Our EPX transfers (the
// stick's commands, usbmsc.c; the LEDs and the probe here) run with those
// polls paused. Clearing INT_EP_CTRL stops new polls, but one already
// scheduled can still run in the next frame: two SOFs (~2 ms).
#define POLLS_PAUSE_TIMEOUT_US 5000

uint32_t usbkbd_polls_pause(void) {
	uint32_t saved = usb_hw->int_ep_ctrl;
	hw_clear_bits(&usb_hw->int_ep_ctrl, saved);
	uint32_t frame = usb_hw->sof_rd;
	uint64_t until = time_us_64() + POLLS_PAUSE_TIMEOUT_US;
	while (((usb_hw->sof_rd - frame) & USB_SOF_RD_BITS) < 2 && time_us_64() < until) {
		tight_loop_contents();
	}
	return saved;
}

void usbkbd_polls_resume(uint32_t saved) {
	uint32_t on = 0;
	for (unsigned i = 1; i <= USB_HOST_INTERRUPT_ENDPOINTS; ++i) {
		if ((saved & (1u << i)) && usb_hw->int_ep_addr_ctrl[i - 1]) { // not closed meanwhile
			on |= 1u << i;
		}
	}
	hw_set_bits(&usb_hw->int_ep_ctrl, on);
}

// Our control transfers (the LEDs, the watchdog's probe) are finished
// before usbkbd_poll returns: one still under way when a USB stick's
// command starts shares the controller's EPX with it (its stages move on in
// the tuh_task calls of usbmsc.c's wait), and TinyUSB master's RP2350 host
// then fails the stick's command now and then (msch_xfer_cb's CBW check;
// bd xv0). A few ms at full speed; one that takes longer is left to run.
#define CONTROL_WAIT_US 20000
static volatile bool leds_busy;

static void control_wait(volatile bool *busy) {
	uint64_t until = time_us_64() + CONTROL_WAIT_US;
	while (*busy && time_us_64() < until) {
		tuh_task();
	}
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type,
	uint16_t len) {
	(void)dev_addr;
	(void)idx;
	(void)report_id;
	(void)report_type;
	(void)len;
	leds_busy = false;
}

// Send the lock LEDs to every keyboard, once nothing is being set up
// (quiet: usb_quiet); tried again on a later poll if TinyUSB was busy.
static void send_leds(bool quiet) {
	if (!leds_pending || !quiet) {
		return;
	}
	leds_pending = false;
	uint32_t polls = 0;
	bool paused = false;
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		if (!keyboards[i].used || !usbkbd_device_ready(keyboards[i].dev_addr)) {
			continue;
		}
		if (!paused) {
			polls = usbkbd_polls_pause();
			paused = true;
		}
		leds_busy = true;
		bool sent = tuh_hid_set_report(keyboards[i].dev_addr, keyboards[i].instance, 0, HID_REPORT_TYPE_OUTPUT,
			&led_report, sizeof led_report);
		usbkbd_log(sent ? "led" : "led?", keyboards[i].dev_addr);
		if (sent) {
			control_wait(&leds_busy);
		} else {
			leds_busy = false;
			leds_pending = true;
		}
	}
	if (paused) {
		usbkbd_polls_resume(polls);
	}
}

// TinyUSB master's tuh_deinit calls hcd_deinit, which frees the RP2350
// driver's spin lock (rp2usb_deinit), before it removes the devices, whose
// hcd_device_close then waits on that lock forever: core 0 hung, with
// interrupts off, on every restart with a device attached (bd xv0).
// usbkbd_reset frees the lock itself once the devices are gone
// (--wrap=rp2usb_deinit, src/usb/CMakeLists.txt; hcd_deinit has a weak
// twin in usbh.c, so its call there cannot be wrapped).
static bool lock_free_later;
void __real_rp2usb_deinit(void);
void __wrap_rp2usb_deinit(void) {
	if (!lock_free_later) {
		__real_rp2usb_deinit();
	}
}

static void host_start(void) {
	tusb_rhport_init_t host = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
	tusb_init(0, &host);
	// hcd_rp2040's interrupt handler panics on a data sequence error (a
	// packet from a device with the wrong DATA0/1 PID), which reboots the
	// board: seen when a stick was plugged in again after hours (bd qo3).
	// The interrupt stays off; usbkbd_data_seq_error finds the error.
	hw_clear_bits(&usb_hw->inte, USB_INTE_ERROR_DATA_SEQ_BITS);
}

bool usbkbd_data_seq_error(void) {
	if (!(usb_hw->intr & USB_INTR_ERROR_DATA_SEQ_BITS)) {
		return false;
	}
	hw_clear_bits(&usb_hw->sie_status, USB_SIE_STATUS_DATA_SEQ_ERROR_BITS);
	usbkbd_log("dataseq", 0);
	usbkbd_request_reset("seqrst");
	return true;
}

// Each device TinyUSB knows, for the text log.
static void log_devices(void) {
	for (uint8_t a = 1; a <= ADDRESSES; ++a) {
		uint16_t vid, pid;
		if (tuh_vid_pid_get(a, &vid, &pid)) {
			text_printf("  device %u %04x:%04x %s\n", a, vid, pid, usbkbd_device_ready(a) ? "ready" : "setting up");
		}
	}
}

void usbkbd_reset(const char *why) {
	usbkbd_log(why, dog.addr);
	log_devices();
	// Every device goes (their unmount callbacks run, so a stick's volume
	// is unmounted by usbmsc_poll) and the controller is reset; the device
	// on the port is then found again and set up like at power-on. The
	// devices go first the usual way, as if the port were unplugged, while
	// the controller still runs: tuh_deinit removes them only after
	// resetting it, where stopping a transfer still under way could wait
	// for good.
	hcd_event_device_remove(0, false);
	tuh_task();
	lock_free_later = true;
	tusb_deinit(0);
	lock_free_later = false;
	__real_rp2usb_deinit();
	memset(keyboards, 0, sizeof keyboards);
	memset(ready, 0, sizeof ready);
	kbd_release_all(&kbd);
	leds_pending = false;
	uint8_t backoff = dog.backoff;
	memset(&dog, 0, sizeof dog);
	dog.backoff = backoff < BACKOFF_MAX ? (uint8_t)(backoff * 2) : BACKOFF_MAX;
	++resets;
	last_reset_ms = now_ms();
	host_start();
}

unsigned usbkbd_resets(void) {
	return resets;
}

void usbkbd_request_reset(const char *why) {
	reset_requested = why;
}

bool usbkbd_reset_pending(void) {
	return reset_requested != NULL;
}

static void probe_done(tuh_xfer_t *xfer) {
	dog.ok = xfer->result == XFER_RESULT_SUCCESS;
	if (!dog.ok) {
		text_printf("probe %u: result %d\n", xfer->daddr, (int)xfer->result);
	}
	dog.busy = false;
	dog.done = true;
}

// The device to probe: the hub if there is one (everything else is behind
// it), else the first one set up; 0 if none.
static uint8_t probe_target(void) {
	for (uint8_t a = CFG_TUH_DEVICE_MAX + 1; a <= ADDRESSES; ++a) {
		if (usbkbd_device_ready(a)) {
			return a;
		}
	}
	for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; ++a) {
		if (usbkbd_device_ready(a)) {
			return a;
		}
	}
	return 0;
}

// Restart the USB host if it looks stuck (see STUCK_SETUP_MS).
static void watchdog(bool quiet) {
	uint32_t now = now_ms();
	uint32_t backoff = dog.backoff;
	if (!quiet) {
		dog.healthy = false;
		if (!dog.setting_up) {
			dog.setting_up = true;
			dog.setup_since = now;
		} else if (now - dog.setup_since > STUCK_SETUP_MS * backoff) {
			usbkbd_reset("stuck");
		}
		return;
	}
	dog.setting_up = false;
	if (dog.busy) {
		if (now - dog.probe_sent > PROBE_DEADLINE_MS * backoff) {
			usbkbd_reset("nprobe");
		}
		return;
	}
	if (dog.done) {
		dog.done = false;
		if (!usbkbd_device_ready(dog.addr)) {
			dog.fails = 0; // it went: not the host's fault
		} else if (dog.ok) {
			dog.fails = 0;
		} else if (++dog.fails >= PROBE_FAILS) {
			usbkbd_reset("probe?");
			return;
		}
	}
	if (!dog.fails) {
		if (!dog.healthy) {
			dog.healthy = true;
			dog.healthy_since = now;
		} else if (now - dog.healthy_since > HEALTHY_MS) {
			dog.backoff = 1;
		}
	}
	if ((int32_t)(now - dog.next_probe) < 0) {
		return;
	}
	dog.next_probe = now + PROBE_EVERY_MS;
	uint8_t addr = probe_target();
	if (addr != dog.addr) {
		dog.addr = addr;
		dog.fails = 0;
	}
	if (!addr) {
		return;
	}
	tuh_xfer_t xfer = {
		.daddr = addr,
		.ep_addr = 0,
		.setup = &probe_request,
		.buffer = probe_status,
		.complete_cb = probe_done,
	};
	uint32_t polls = usbkbd_polls_pause();
	dog.busy = true;
	dog.probe_sent = now;
	if (!tuh_control_xfer(&xfer)) {
		usbkbd_polls_resume(polls);
		dog.busy = false;
		text_printf("probe %u: not sent\n", addr);
		if (++dog.fails >= PROBE_FAILS) {
			usbkbd_reset("probe!");
		}
		return;
	}
	control_wait(&dog.busy);
	usbkbd_polls_resume(polls);
}

void usbkbd_init(kbd_output_t output, kbd_setup_t setup, void *ctx) {
	kbd_init(&kbd, output, ctx);
	kbd_set_setup_key(&kbd, setup);
	led_report = kbd_leds(&kbd);
#ifdef BOARD_USB_VBUS_EN_PIN
	// The board switches the port's 5 V; the RP2350 cannot supply it.
	gpio_init(BOARD_USB_VBUS_EN_PIN);
	gpio_put(BOARD_USB_VBUS_EN_PIN, BOARD_USB_VBUS_EN_LEVEL);
	gpio_set_dir(BOARD_USB_VBUS_EN_PIN, GPIO_OUT);
#endif
	host_start();
}

void usbkbd_set_reset_key(kbd_setup_t reset) {
	kbd_set_reset_key(&kbd, reset);
}

void usbkbd_poll(const kbd_modes_t *modes) {
	poll_modes = modes;
	tuh_task();
	usbkbd_data_seq_error();
	if (reset_requested) {
		if (resets && now_ms() - last_reset_ms < REQUEST_GAP_MS * dog.backoff) {
			if (!reset_waiting) {
				reset_waiting = true;
				usbkbd_log("rstwait", 0);
			}
		} else {
			const char *why = reset_requested;
			reset_requested = NULL;
			reset_waiting = false;
			usbkbd_reset(why);
		}
	}
	bool quiet = usb_quiet();
	send_leds(quiet);
	watchdog(quiet);
	kbd_tick(&kbd, modes, now_ms());
	poll_modes = &no_modes;
}

unsigned usbkbd_count(void) {
	unsigned n = 0;
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		n += keyboards[i].used;
	}
	return n;
}

// TinyUSB callbacks (from tuh_task). None starts a control transfer:
// TinyUSB may be in the middle of setting up a device (see usb_quiet).

void tuh_mount_cb(uint8_t dev_addr) {
	usbkbd_log("up", dev_addr);
	if (dev_addr <= ADDRESSES) {
		ready[dev_addr] = true;
	}
}

void tuh_umount_cb(uint8_t dev_addr) {
	usbkbd_log("down", dev_addr);
	if (dev_addr <= ADDRESSES) {
		ready[dev_addr] = false;
	}
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
	(void)desc_report;
	(void)desc_len;
	// Boot keyboards only; TinyUSB has put them in boot protocol. Other HID
	// interfaces (mice, media keys) are left alone.
	if (tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_KEYBOARD) {
		return;
	}
	usbkbd_log("kbd", dev_addr);
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		if (!keyboards[i].used) {
			keyboards[i].used = true;
			keyboards[i].dev_addr = dev_addr;
			keyboards[i].instance = instance;
			break;
		}
	}
	leds_pending = true; // once the keyboard is set up (send_leds)
	tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		if (keyboards[i].used && keyboards[i].dev_addr == dev_addr && keyboards[i].instance == instance) {
			usbkbd_log("kbd-", dev_addr);
			keyboards[i].used = false;
			// Its keys can no longer be released: stop any repeat.
			kbd_release_all(&kbd);
		}
	}
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
	if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
		uint8_t boot[8] = {0};
		memcpy(boot, report, len < sizeof boot ? len : sizeof boot);
		if (kbd_report(&kbd, boot, poll_modes, now_ms())) {
			led_report = kbd_leds(&kbd);
			leds_pending = true;
		}
	}
	tuh_hid_receive_report(dev_addr, instance);
}
