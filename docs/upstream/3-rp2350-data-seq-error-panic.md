<!-- A comment for hathach/tinyusb#3870 (RP2XXX MIDI Host Panics (Data Seq Error)), not a new issue:
     #3870 asks for exactly this (handle the error without panicking), and #1997 and #3964 hit the same
     panic. Points at #4213 as a likely way the toggles get out of step. -->

Another case of the same panic, on an **RP2350** host this time (the panic is in code the RP2040 and
RP2350 share; unchanged on master 006af6325, 2026-10-24).

**Setup:** RP2350 native USB host (full speed), clk_sys 400 MHz, pico-sdk 2.3.1, CFG_TUSB_OS = OPT_OS_PICO.
On-board WCH CH334 hub (1a86:8091); behind it a USB keyboard (7807:dccb, boot keyboard) and a USB stick
(24a9:205a, MSC bulk-only).

**When:** replugging the stick into the hub, about one replug in five over an afternoon, and once after the
board had sat idle ~9.5 hours with the stick out. Always while the stick was being set up behind the hub
(TinyUSB still listed it as being enumerated), never during bulk reads. The whole application goes down,
the keyboard with it, for one bad packet from one device.

Controller state 20 s after the panic (the controller kept polling the interrupt endpoints): INTS `00400`
(STALL), SIE_CTRL `20008208`, SIE_STATUS `70800205` (ACK_REC, STALL_REC, NAK_REC, ENDPOINT_ERROR, full
speed), BUFF_STATUS `0`.

Our USB log around one caught occurrence, with the workaround below (seconds):

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

**A likely cause** is #4213: a failed EPX transfer (an RX timeout or STALL while the stick is set up)
keeps the data toggle it advanced when it armed its buffers, so the next IN on that endpoint expects
the wrong PID. That fits a device being set up behind a hub. We have not established which endpoint the
bad packet came from.

**Application-side workaround** (the handler is static and registered directly with
`irq_add_shared_handler`, so it cannot be wrapped): after every `tusb_init()`,
`hw_clear_bits(&usb_hw->inte, USB_INTE_ERROR_DATA_SEQ_BITS)`, so `hcd_rp2040_irq()` (which reads
`usb_hw->ints`) never sees it; from the main loop and while waiting on an MSC command, check the raw
`usb_hw->intr & USB_INTR_ERROR_DATA_SEQ_BITS`, clear `SIE_STATUS.DATA_SEQ_ERROR`, fail any MSC command
under way, and restart the host. The error then costs a ~3 s host restart instead of the application.

**Proposed fix** (builds for RP2040 and RP2350; not run on hardware yet, our firmware still masks the
interrupt): clear the error and, if EPX is receiving, stop it and fail its transfer as for an RX timeout,
before BUFF_STATUS, so a buffer completed with the wrong PID is not delivered as a success (the ordering
#3582 proposes). Interrupt endpoints need nothing: their buffer stays armed with the PID they expect.
https://github.com/arlaneenalra/tinyusb/tree/fix/rp2-hcd-data-seq-no-panic
(PR: https://github.com/hathach/tinyusb/compare/master...arlaneenalra:tinyusb:fix/rp2-hcd-data-seq-no-panic)
It pairs with the #4213 fix: https://github.com/arlaneenalra/tinyusb/tree/fix/rp2-hcd-failed-xfer-pid-rollback
