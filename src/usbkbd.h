#ifndef USBKBD_H
#define USBKBD_H

// USB keyboards on the native USB port (TinyUSB host, boot protocol),
// translated by keyboard.c. Runs on core 0 from the main loop.
//
// The port must be powered: in host mode the board does not switch 5 V onto
// the USB connector's VBUS itself (see README, "USB keyboard").

#include "keyboard.h"

// Start the USB host. Keys are passed to output as VT100 byte sequences.
// The SET-UP key (F12 or Pause) calls setup instead.
void usbkbd_init(kbd_output_t output, kbd_setup_t setup, void *ctx);

// F11 calls reset instead of sending a sequence (kbd_set_reset_key).
void usbkbd_set_reset_key(kbd_setup_t reset);

// Run the USB host stack and key repeat. Call often from the main loop.
void usbkbd_poll(const kbd_modes_t *modes);

// Keyboards (HID boot keyboard interfaces) currently attached.
unsigned usbkbd_count(void);

// Whether TinyUSB has finished setting up the device at dev_addr (for
// usbmsc_info).
bool usbkbd_device_ready(uint8_t dev_addr);

// A short log of USB host events for the debug status line (the keyboard
// may be what stopped working): what (a short static name) for device
// dev. A repeat of the last event only bumps its count and time.
void usbkbd_log(const char *what, uint8_t dev);

// The last few events, oldest first, as " 123s name2 ..." (seconds since
// boot, the name, the device address, xN for repeats).
void usbkbd_log_text(char *out, size_t size);

#endif
