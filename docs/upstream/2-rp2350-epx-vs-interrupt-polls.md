# rp2040 host (RP2350): a bulk transfer started on EPX while the controller polls interrupt endpoints "completes" at once with 0 bytes

**TinyUSB:** master 13f9a36a8 (EPX for non-interrupt endpoints, #3561 and later)
**Hardware:** RP2350 native USB host (full speed), clk_sys 400 MHz, pico-sdk 2.3.1.
On-board WCH CH334 hub (1a86:8091); behind it a USB keyboard (7807:dccb, boot keyboard, HID interrupt
endpoints) and a USB stick (24a9:205a, MSC bulk-only).
**Related:** #3964 (EPX with a hub behind it, RP2040; its "first INQUIRY never completes" shape looks
like this one).

## Symptom

MSC reads fail now and then: `msch_xfer_cb` asserts in MSC_STAGE_CMD (the CBW check), the interface is
then stuck until the stick is replugged (no Bulk-Only reset recovery). The CBW completion arrives as
**XFER_RESULT_SUCCESS with 0 bytes** on the correct endpoint (ep 02):

```
MSC CBW cb bad: ep 02 (out 02 in 81) ev 0 len 0; last callbacks (stage ep ev len cmd total):
  3 81 0 13 28 512     <- previous command's CSW, fine
  1 02 0 0 28 512      <- this CBW: SUCCESS, 0 bytes
```

With TinyUSB 0.18 (bulk on interrupt-endpoint slots) the same hardware read the stick without errors
(slowly, ~61 KB/s).

## What the controller does

A RAM ring of EPX events (armed buffer control, every BUFF_STATUS with BUFF_CPU_SHOULD_HANDLE, the raw
buffer control, sie_status, ints, nak_poll, timestamps in us) around a failure. Buffer control low half:
0x8000 FULL, 0x4000 LAST, 0x2000 DATA1, 0x0400 AVAIL, low bits LEN.

| | armed | BUFF_STATUS (buf 0, single-buffered) | arm to done | gap after the previous CSW |
|---|---|---|---|---|
| good CBW | `F41F` (or `D41F`) | `601F` / `401F` (LEN 31 kept) | 29-30 us | ~7 us |
| bad CBW | `D41F` (or `F41F`) | `4000` (LEN 0, PID 0, AVAIL clear) | 14-18 us | ~216 us |

- 31 bytes plus the token need ~23 us at full speed; the bad completion comes too soon for the data to
  have gone out.
- No error bits in SIE_STATUS (`50000205`), no RX timeout, no STALL; NAK_POLL `00104010`
  (STOP_EPX_ON_NAK never set, no EPX preemption involved).
- Re-arming the same buffer control and restarting the transfer after the bogus completion never
  completes.
- **Every** failing CBW was started after EPX had been idle ~216 us (the application processing a 4 KB
  chunk between FatFs reads); every transfer started within ~7 us of the previous completion worked.

While EPX is idle the controller polls the interrupt endpoints by itself (the hub's status endpoint,
the keyboard behind the hub). Our reading: starting EPX (writing the buffer control, then START_TRANS)
while such a poll is under way makes the controller finish the EPX transfer at once with nothing sent.

## Confirmation / workaround

Clearing INT_EP_CTRL, waiting two SOFs (a poll already scheduled can still run in the next frame, as
noted for the interrupt-endpoint abort in #4177), running the MSC commands, then setting the bits again
(skipping slots closed meanwhile): no failures over repeated 1.7 MB firmware reads and installs from the
stick, 64-sector READ10s, plugs and unplugs; read speed ~10x that of 0.18. We do the same around our own
control transfers (keyboard LEDs, a periodic GET_STATUS).

Not established: whether this is specific to the RP2350, to a low-speed device behind the hub (PRE),
or to the CH334. A proper fix probably belongs in hcd_rp2040: do not start an EPX transfer while an
interrupt endpoint poll may be in progress (or detect the 0-byte completion of an OUT armed with data
and recover).

## Side note: BUFF_CPU_SHOULD_HANDLE

We first suspected `rp2usb_xfer_continue()` reading the BUF1 half for a single-buffered transfer.
Forcing buf_id = 0 for single-buffered endpoints made things worse, and the traces above show the bad
completions with buf_id 0, so that was not it (RP2040-E4 is fixed on RP2350).
