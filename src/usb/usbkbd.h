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

// The same events and TinyUSB's own error messages as text for a file (the
// SD card's USBLOG.TXT): "12.345 name 2" CR LF lines, the time since boot
// in seconds. n bytes are waiting at the returned pointer; lost: some did
// not fit since the last usbkbd_log_done. Main loop only, outside FatFs:
// the text keeps growing past n meanwhile (TinyUSB's interrupt handler).
const char *usbkbd_log_pending(size_t *n, bool *lost);
// Drop the first n bytes, once written.
void usbkbd_log_done(size_t n);
// At start-up, before anything is logged: keep the text a crash's reboot
// left unwritten (keep, and it is intact), else start empty.
void usbkbd_log_restore(bool keep);
// One line of text for the file (not the status line's events).
void usbkbd_log_line(const char *line);

// Pause the controller's own interrupt endpoint polls (hub, keyboards)
// around a transfer on EPX (a USB stick's commands): one started during a
// poll was lost (bd xv0). Takes ~2 ms; returns what to resume.
uint32_t usbkbd_polls_pause(void);
void usbkbd_polls_resume(uint32_t saved);

// Restart the USB host: every device goes and the one on the port is set
// up again, as at power-on. usbkbd_poll does it by itself when the host
// looks stuck (a device being set up for over 20 s, or the hub not
// answering); why is logged.
void usbkbd_reset(const char *why);
// Restarts since power-on.
unsigned usbkbd_resets(void);
// Have usbkbd_poll restart the host (from inside FatFs or a callback,
// where it cannot be done at once); one soon after the last waits a while.
void usbkbd_request_reset(const char *why);
bool usbkbd_reset_pending(void);
// A data sequence error since the last call (a packet the controller
// should not have taken: the transfer under way is not to be trusted, if
// it ever finishes)? Then the host restart is requested.
bool usbkbd_data_seq_error(void);

#endif
