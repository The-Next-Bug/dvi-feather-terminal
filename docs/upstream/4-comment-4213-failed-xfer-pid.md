<!-- A comment for hathach/tinyusb#4213 (rp2 hcd: a failed EPX transfer keeps the data toggle it advanced
     when armed), offering a fix: #4213 has no PR. -->

A proposed fix, as the issue describes it: split the toggle/accounting undo out of `epx_save_context()`
(as `epx_undo_unsent()`) and call it on `xfer_complete_isr()`'s failure path too, before
`epx_drop_buffers()`, except for a SETUP (no data buffers). Builds for RP2040 and RP2350; not yet run on
hardware.

https://github.com/arlaneenalra/tinyusb/tree/fix/rp2-hcd-failed-xfer-pid-rollback
(PR: https://github.com/hathach/tinyusb/compare/master...arlaneenalra:tinyusb:fix/rp2-hcd-failed-xfer-pid-rollback)

We think this is also the likely way an RP2350 host behind a hub reaches the "Data Seq Error" panic
when a USB stick is replugged (#3870, our comment there); the companion change that fails the transfer
instead of panicking is https://github.com/arlaneenalra/tinyusb/tree/fix/rp2-hcd-data-seq-no-panic.
