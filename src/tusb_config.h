#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// TinyUSB: host only, on the RP2040/RP2350 native USB port (root hub port
// 0), for HID keyboards. CFG_TUSB_MCU and CFG_TUSB_OS come from pico-sdk.

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 0
#define CFG_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))

// Plenty for keyboard descriptors; larger ones are skipped (not needed in
// boot protocol).
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// One hub, as many keyboards have one built in (or sit behind one).
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (CFG_TUH_HUB + 3)
// A keyboard often has 2-3 HID interfaces (keys, media keys, mouse).
#define CFG_TUH_HID 6
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#endif
