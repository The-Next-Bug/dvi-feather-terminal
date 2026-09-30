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

// Run the USB host stack and key repeat. Call often from the main loop.
void usbkbd_poll(const kbd_modes_t *modes);

// Keyboards (HID boot keyboard interfaces) currently attached.
unsigned usbkbd_count(void);

#endif
