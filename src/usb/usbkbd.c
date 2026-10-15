#include "usbkbd.h"

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

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

static uint32_t now_ms(void) {
	return to_ms_since_boot(get_absolute_time());
}

void usbkbd_log(const char *what, uint8_t dev) {
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

// No device part way through being set up. TinyUSB has a single control
// transfer for the whole host: one of ours (the LEDs) in the middle of a
// device's setup makes its next request fail, which leaves that device
// half set up and TinyUSB enumerating it for good: no other device (a USB
// stick plugged in later) is ever set up.
static bool usb_quiet(void) {
	for (uint8_t a = 1; a <= ADDRESSES; ++a) {
		uint16_t vid, pid;
		if (tuh_vid_pid_get(a, &vid, &pid) && !usbkbd_device_ready(a)) {
			return false;
		}
	}
	return true;
}

// Send the lock LEDs to every keyboard, once nothing else needs the
// control transfer; tried again on a later poll if it was busy.
static void send_leds(void) {
	if (!leds_pending || !usb_quiet()) {
		return;
	}
	leds_pending = false;
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		if (!keyboards[i].used || !usbkbd_device_ready(keyboards[i].dev_addr)) {
			continue;
		}
		bool sent = tuh_hid_set_report(keyboards[i].dev_addr, keyboards[i].instance, 0, HID_REPORT_TYPE_OUTPUT,
			&led_report, sizeof led_report);
		usbkbd_log(sent ? "led" : "led?", keyboards[i].dev_addr);
		if (!sent) {
			leds_pending = true;
		}
	}
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
	tusb_rhport_init_t host = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
	tusb_init(0, &host);
}

void usbkbd_set_reset_key(kbd_setup_t reset) {
	kbd_set_reset_key(&kbd, reset);
}

void usbkbd_poll(const kbd_modes_t *modes) {
	poll_modes = modes;
	tuh_task();
	send_leds();
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
