#include "usbkbd.h"

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

// Attached keyboard interfaces, for the lock LEDs.
static struct {
	bool used;
	uint8_t dev_addr, instance;
} keyboards[CFG_TUH_HID];

static kbd_t kbd;
static const kbd_modes_t no_modes;
// The modes for reports that arrive during this usbkbd_poll.
static const kbd_modes_t *poll_modes = &no_modes;
// The LED output report; must stay valid until the transfer completes.
static uint8_t led_report;

static uint32_t now_ms(void) {
	return to_ms_since_boot(get_absolute_time());
}

static void set_leds(uint8_t dev_addr, uint8_t instance) {
	// Fails if the keyboard's control pipe is busy; the next lock key press
	// sets them again.
	tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT, &led_report, sizeof led_report);
}

void usbkbd_init(kbd_output_t output, kbd_setup_t setup, void *ctx) {
	kbd_init(&kbd, output, ctx);
	kbd_set_setup_key(&kbd, setup);
	led_report = kbd_leds(&kbd);
	tusb_rhport_init_t host = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
	tusb_init(0, &host);
}

void usbkbd_set_reset_key(kbd_setup_t reset) {
	kbd_set_reset_key(&kbd, reset);
}

void usbkbd_poll(const kbd_modes_t *modes) {
	poll_modes = modes;
	tuh_task();
	kbd_tick(&kbd, modes, now_ms());
	poll_modes = &no_modes;
}

unsigned usbkbd_count(void) {
	unsigned n = 0;
	for (size_t i = 0; i < count_of(keyboards); ++i)
		n += keyboards[i].used;
	return n;
}

// TinyUSB callbacks (from tuh_task).

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
	(void)desc_report;
	(void)desc_len;
	// Boot keyboards only; TinyUSB has put them in boot protocol. Other HID
	// interfaces (mice, media keys) are left alone.
	if (tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_KEYBOARD)
		return;
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		if (!keyboards[i].used) {
			keyboards[i].used = true;
			keyboards[i].dev_addr = dev_addr;
			keyboards[i].instance = instance;
			break;
		}
	}
	set_leds(dev_addr, instance);
	tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
	for (size_t i = 0; i < count_of(keyboards); ++i) {
		if (keyboards[i].used && keyboards[i].dev_addr == dev_addr && keyboards[i].instance == instance) {
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
			for (size_t i = 0; i < count_of(keyboards); ++i)
				if (keyboards[i].used)
					set_leds(keyboards[i].dev_addr, keyboards[i].instance);
		}
	}
	tuh_hid_receive_report(dev_addr, instance);
}
