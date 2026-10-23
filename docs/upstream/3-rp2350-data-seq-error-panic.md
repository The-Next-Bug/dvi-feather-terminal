# rp2040 host: a data sequence error panics in hcd_rp2040_irq ("Data Seq Error"), taking the whole application down

**TinyUSB:** master 13f9a36a8 (the panic is unchanged on master as of 2026-10-23)
**Hardware:** RP2350 native USB host (full speed), clk_sys 400 MHz, pico-sdk 2.3.1, CFG_TUSB_OS = OPT_OS_PICO.
On-board WCH CH334 hub (1a86:8091); behind it a USB keyboard (7807:dccb, boot keyboard) and a USB stick
(24a9:205a, MSC bulk-only).

## What happens

Now and then, plugging the USB stick back into the hub makes the controller report a data sequence error
(an IN packet with the wrong DATA0/DATA1 PID). `hcd_rp2040_irq()` (src/portable/raspberrypi/rp2040/hcd_rp2040.c)
answers it with a panic:

```c
  if (status & USB_INTS_ERROR_DATA_SEQ_BITS) {
    usb_hw_clear->sie_status = USB_SIE_STATUS_DATA_SEQ_ERROR_BITS;
    panic("Data Seq Error \n");
  }
```

The application stops dead, from inside the USB interrupt. The keyboard and every other device on the
bus go with it, although a single bad packet from one device is something a host is expected to survive.

- It happened on about one replug in five over an afternoon of testing, and once after the board had sat
  idle for ~9.5 hours with the stick unplugged.
- It happens while the stick is being set up behind the hub (TinyUSB still listed it as being enumerated
  when we caught the error), not during bulk reads.
- The controller state 20 s after the panic (the controller kept polling the interrupt endpoints):
  INTS `00400` (STALL), SIE_CTRL `20008208`, SIE_STATUS `70800205` (ACK_REC, STALL_REC, NAK_REC,
  ENDPOINT_ERROR, full speed), BUFF_STATUS `0`.

Our USB log around one caught occurrence (with the workaround below; times in seconds):

```
931.195 down 2          <- stick unplugged
947.091 dataseq 0       <- stick plugged in again: INTR ERROR_DATA_SEQ set
947.091 seqrst 6        <- host restart requested; the devices at that moment:
947.091   device 1 7807:dccb ready          (keyboard)
947.091   device 2 24a9:205a setting up     (the stick)
947.091   device 6 1a86:8091 ready          (CH334 hub)
948.916 up 1            <- keyboard back
949.778 up 2            <- stick mounted
```

## Suggested fix

Treat a data sequence error as a failed transfer, not a fatal error:

- clear `SIE_STATUS.DATA_SEQ_ERROR`;
- if EPX is active, stop it the way the RX timeout path does and complete it with `XFER_RESULT_FAILED`
  (usbh then retries or gives up on the device, as for any other failed transfer);
- for an interrupt endpoint polled by the controller, fail (or just drop) that endpoint's transfer.

Not established: which endpoint the bad packet came from (EPX doing the stick's enumeration, or one of
the interrupt endpoints the controller was polling at the time), and what the controller does with the
transfer afterwards (whether it completes, with what data, or never does).

## Workaround (application side)

The handler is static and registered directly with `irq_add_shared_handler`, so it cannot be wrapped.
What works:

- after every `tusb_init()`, clear the interrupt enable: `hw_clear_bits(&usb_hw->inte, USB_INTE_ERROR_DATA_SEQ_BITS)`
  (`hcd_rp2040_irq` reads `usb_hw->ints`, so it never sees the error);
- from the main loop, and while waiting for an MSC command, check the raw `usb_hw->intr & USB_INTR_ERROR_DATA_SEQ_BITS`;
  when it is set, clear `SIE_STATUS.DATA_SEQ_ERROR`, fail any MSC command under way (its data may hold a
  packet received twice), and restart the host (remove the devices, `tusb_deinit()`, `tusb_init()`).

With that, the error costs a ~3 s host restart (the keyboard is set up again, the stick is mounted
again) instead of the application.
