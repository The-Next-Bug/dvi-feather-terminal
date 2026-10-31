#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// TinyUSB: host only, on the RP2350 native USB port (root hub port
// 0), for HID keyboards and USB sticks (usbmsc.c). CFG_TUSB_MCU and
// CFG_TUSB_OS come from pico-sdk.

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 0
#define CFG_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED

// Its messages (CFG_TUSB_DEBUG, set by the top CMakeLists.txt) go to the
// USB log (usbkbd.c), not printf.
#define CFG_TUSB_DEBUG_PRINTF usbkbd_tusb_printf

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))

// Plenty for keyboard descriptors; larger ones are skipped (not needed in
// boot protocol).
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// Three hubs (bd rsu): the Frankless's own, one plugged into it and a
// keyboard's built-in one behind that (tried with two; the third, and the
// refusal of a fourth, not yet on hardware); ~340 bytes of SRAM each. Devices
// (not hubs) get CFG_TUH_DEVICE_MAX addresses: keyboards and sticks
// (usbvol.c uses one at a time). A hub or device past these limits is
// never set up and, as TinyUSB leaves its setup unfinished, no device
// plugged in later is either, until the watchdog turns its hub port off
// (usbkbd.c refuse_dev0).
#define CFG_TUH_HUB 3
#define CFG_TUH_DEVICE_MAX (CFG_TUH_HUB + 4)
// A keyboard often has 2-3 HID interfaces (keys, media keys, mouse).
#define CFG_TUH_HID 6
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
// A device that fails to be set up gets this many tries, each after a
// fresh port reset (TinyUSB's default is 2): after a host restart
// (usbkbd_reset) a keyboard behind the hub, never powered off, missed its
// first requests and was given up on for good (bd xv0).
#define CFG_TUH_ENUM_ATTEMPT_MAX 5
// Mass storage: TinyUSB keeps one per device and only ever uses LUN 0 (a
// multi-slot card reader shows its first slot).
#define CFG_TUH_MSC 1
#define CFG_TUH_MSC_MAXLUN 1

#endif
