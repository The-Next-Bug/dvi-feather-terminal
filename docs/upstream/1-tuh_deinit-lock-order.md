# rp2040 host: tuh_deinit() hangs with a device attached (hcd_deinit frees rp2usb_lock before remove_device_tree)

**TinyUSB:** master 13f9a36a8 (also present since rp2usb_deinit() was added to hcd_deinit())
**Port:** rp2040 hcd (native USB), seen on RP2350, pico-sdk 2.3.1, CFG_TUSB_OS = OPT_OS_PICO

## What happens

Calling `tuh_deinit(0)` (or `tusb_deinit(0)`) while any device is attached hangs core 0 forever with
interrupts disabled.

`tuh_deinit()` (src/host/usbh.c):

```c
hcd_int_disable(rhport);
TU_ASSERT(hcd_deinit(rhport));       // -> rp2usb_deinit() -> critical_section_deinit(&rp2usb_lock)
_usbh_controller_id = TUSB_INDEX_INVALID_8;
remove_device_tree(rhport, 0, 0);    // -> usbh_device_close() -> hcd_device_close()
```

`critical_section_deinit()` unclaims the spin lock and sets `rp2usb_lock.spin_lock = NULL`. Then
`hcd_device_close()` calls `rp2usb_critical_enter()` -> `critical_section_enter_blocking()` on the NULL
lock and spins forever (the "lock" at address 0 reads non-zero).

Captured PC/LR at the hang (addr2line):

```
PC: spin_lock_blocking <- critical_section_enter_blocking <- rp2usb_critical_enter <- hcd_device_close (hcd_rp2040.c:592)
LR: usbh_device_close (usbh.c:432) <- remove_device_tree (usbh.c:1728)
```

## Suggested fix

Remove the devices before tearing down the controller: in `tuh_deinit()` call
`remove_device_tree(rhport, 0, 0)` before `hcd_deinit(rhport)` (the controller is still running, so
`hcd_device_close()` can also stop an EPX transfer still under way: `sie_stop_xfer()` waits for
STOP_TRANS to clear, which a controller already reset might never do). Alternatively free the lock
after the devices are gone.

## Workaround (application side)

`usbh.c` has a weak `hcd_deinit()`, so `-Wl,--wrap=hcd_deinit` does not catch the call from
`tuh_deinit()` (it binds inside usbh.o). What works:

- queue a whole-port remove first and let the stack handle it while the controller runs:
  `hcd_event_device_remove(0, false); tuh_task();`
- `-Wl,--wrap=rp2usb_deinit`, deferring `__real_rp2usb_deinit()` until after `tusb_deinit()` returns.
