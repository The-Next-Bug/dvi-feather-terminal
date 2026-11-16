# Idea: an explicit end for print jobs (WordStar first)

Status: plan only, not started, no bd ticket. Written 2026-11-16 from
this firmware's printer code and docs, the Zimodem fork
(~/fz80/Zimodem, `zimodem/zprint.ino`) and the WordStar 4 reconstruction
(~/fz80/wordstar-4: `src/user/userarea.mac`, `src/ws/WS.OVR/D.asm`, its
wiki's `user-area.md` and `printing.md`). Nothing has been tried.

## The problem

CP/M's LST: has no "end of job", so a print job ends only after the
Printer panel's **Job ends after** time (5-120 s, 30 by default) without
a byte. For WordStar that causes two problems:

1. **Jobs split.** Background printing stalls while you type, at
   "Pausing at top of page" (which waits for a key), and between pages. A
   stall longer than the timeout ends the job, and the rest of the
   document becomes a second job. For `PS` with IPP raw, the second job
   has no prolog, so the printer cannot print it.
2. **The printout is late.** After the last byte, the printer still
   waits out the idle time. A longer timeout fixes (1) and makes (2)
   worse.

## What exists

### This firmware

`OUT 05h,01h` already ends a job (`reference/ports.md`, `printjob_end()`
in `src/printer/printjob.c`, `apps/terminal/printer_glue.c`). It sets
`end_asked`, takes no more data and goes to `CLOSE_WAIT`. With no job
running it does nothing. None of the BIOSes (`bios.asm`,
`cpm3/bnkbios3.asm`, `mpm/bnkxios.mac`) buffer LIST output, so once a
program's last BDOS 5 returns, every byte is in the firmware.

### Zimodem

`ZPrint::loop()` ends a print job **only** on S61's idle time
(`currentExpiresTimeMs`) or on the `+++` escape. Then it writes the last
HTTP chunk (`"0\r\n\r\n"`) and answers `OK`. There is no "end now", and
`+++` is unreliable after a backlog (see `cpm/printing.md`).

**So `OUT 05h,01h` sets where a job ends, but the printer still waits
S61 seconds, and S61 has to be as long as the firmware's idle time.** A
prompt printout needs a change in the Zimodem fork.

Stock Zimodem answers `OK` to an S-register it does not know
(`zcommand.ino`: `default: break;`). S61, S62 and S63 are taken.

### WordStar 4's user area

| Hook | Addr | When it is called (WS.OVR segment D) |
|---|---|---|
| `ULPORT` | 059Fh | before printing (84E0h), after the IOBYTE/LSTDEV set-up, before the MP/M attach (BDOS 158) and `ULINI`; HL = PRTNAME |
| `ULUNPT` | 05A2h | from `DISKFLUSH` (851Ah), after the driver's end and `ULUNI`, before the IOBYTE restore and the MP/M detach (BDOS 159); HL = PRTNAME |
| `ULOSTA` | (user area) | `LOSTAT` (856Fh) calls it in place of BIOS LISTST; returns 0 busy, FFh ready |
| `ULSTO` | (user area) | `LO` (8599h) calls it with A = the character, in place of BDOS 5 |
| `MORPAT` | 045Bh | free patch space: zeros in `lib/wordstar/WS.COM`, unused by wordstar-4's `wsinstall.py` |

Each hook is 3 bytes (`NOP NOP RET`). WordStar calls it only if its
first byte is not zero.

The paths that reach `ULUNPT` (`D.asm`):

- **Normal end:** `PRTDONE` (7252h) → `PDRV1E` → `PRTCLOSE` → `ULUNI` →
  `DISKFLUSH` → `ULUNPT`.
- **^U abort, or a driver or prompt stopping:** `PRTABORT` (7211h) →
  `PDRV1B` → the same path from 7258h.
- **Several copies:** `PRTNEXTCOPY` loops back to `PRINTJOB` without
  `DISKFLUSH`, so all copies are one job.
- **Printing to a file (`PS>FILE.PS`):** `DISKFLUSH` closes the file and
  returns before `ULUNPT`, so the ports are never touched.
- **Index / table of contents:** skips `DISKFLUSH`. Those print to a
  file anyway.

`DISKFLUSH` enters `ULUNPT` with A = the hook's own first byte. So a
3-byte `out (05h),a / ret` in place would send D3h, not 01h. The hook
has to jump to `MORPAT`.

Every driver checks `LSOSTA` before each `LSTOUT`, and while the printer
is busy it suspends and returns "busy" to WordStar, which goes back to
editing (wordstar-4 `F_PS.asm:176`, `drivers.toml` 8831h). `LSTSTR`
(used for `ULINI`/`ULUNI`) calls `LO` without checking status.

## Feasibility

| Part | Feasible? | Notes |
|---|---|---|
| WordStar tells the firmware where a job ends | **Yes, now**, with no firmware change | `ULUNPT` → `ld a,1 / out (05h),a / ret` in `MORPAT` |
| Jobs survive WordStar's pauses | **Yes**, with a small firmware addition | a "held job" hint from `ULPORT` |
| The printer gets the job as soon as it ends | **Only with a Zimodem change** | framed print data with an explicit end |
| Stock Zimodem keeps working | Yes | fall back to today's idle-time ending |

## Design

### 1. Zimodem fork: framed print data

An opt-in mode for print mode (a new S-register, say **S64=1**, set per
job). While it is on, 10h (DLE) is an escape in the serial stream:

- `DLE DLE` → a literal 10h
- `DLE ETX` (10h 03h) → **end the job now**: flush `pbuf`, write
  `"0\r\n\r\n"`, then `switchBackToCommandMode(false)` → `OK`. This is
  the timeout branch of `loop()`.
- `DLE` + anything else: reserved

In framed mode, also:

- `+++` is not processed. That removes printing.md's "a line that begins
  with `+++` ends a job early". The attention sequence runs only outside
  a job, so it is not affected.
- De-escape before the ASCII/PETSCII fix-ups in `serialIncoming()`, so
  `AT+PRINTA` gets framing too.
- S61 is only a safety net.

Changes: `zprint.ino`/`zprint.h` (a flag, a DLE state, de-escaping,
skipping `+++`, the end branch) and `zcommand.ino` (S64). About 40
lines.

Stock Zimodem cannot be detected by its S-register answer. Either add a
Printer panel setting, "Modem: stock / framed (fork)", default stock
(recommended), or read the fork's `ATI` string.

### 2. This firmware

`src/printer/printjob.[ch]`, `machine.c` (port 05h out):

- **Framed mode** (when the setting is on): send `ATS64=1` after
  `ATS61`. While printing, double every 10h byte (the TX-room check must
  allow for 2 bytes). On `printjob_end()` or the idle time, send `DLE
  ETX`, then go to `CLOSE_WAIT`. `OK` then comes at once. Set S61 long
  (for example the panel time × 4, capped at a few minutes) as the safety
  net.
- **Held jobs, `OUT 05h,02h`:** "this job (or the next one, if none is
  running) is ended by command". The panel's idle time does not end a
  held job, but a long cap (for example 30 min) does, in case the program
  dies. `01h`, the cap, or a settings change clears the hold. Without
  framing, a held job also needs a long S61, so its printout is still
  late. The hold is most useful together with framing.
- **Optional, text or raw for each job, `OUT 05h,03h` / `04h`:** the
  next job is sent as raw / text, overriding the panel's "IPP sends" for
  that job.
- **Optional, generic: end the job on warm boot.** Put `ld a,1 / out
  (05h),a` in `WBOOT` (`bios.asm`, `cpm3/bnkbios3.asm`). Then `PIP
  LST:=FILE` and every other transient end their job when they exit, with
  no WordStar patch. ^C warm boots too, which also ends the job. MP/M has
  no warm boot per process.

Docs: `reference/ports.md` (the new 05h commands, framed mode),
`cpm/printing.md` (job steps 3 and 5, `+++` in framed mode),
`cpm/wordstar.md` (WordStar ends its own jobs; "Job ends after" does not
apply to it; how to patch by hand with WSCHANGE).

### 3. WordStar: two ways (in `src/machine/cpm/ws4/ws4_patch.py`)

Port 05h is specific to this machine, so the patch goes in this
project's patcher, not in wordstar-4's generic `make patched`. Patch
WS.COM, WS80.COM and WSADM.COM. The user area is at file offset =
address − 100h. Check the original bytes first, as the other patches do.

**(a) Keep LST:, add the start and end hooks.**

| Where | Original | New |
|---|---|---|
| `ULPORT` 059Fh | `00 00 C9` | `C3 lo hi` → PRTBEGIN |
| `ULUNPT` 05A2h | `00 00 C9` | `C3 lo hi` → PRTEND |
| `MORPAT` | zeros | `PRTBEGIN: 3E 02 D3 05 C9`, `PRTEND: 3E 01 D3 05 C9` |

Without firmware support for 02h, PRTBEGIN is ignored and does no harm.
Even without framing, PRTEND gives correct job boundaries: a second
print started within the idle time is no longer added to the first job.
`ULUNI` (the uninit string) cannot carry the end, because it goes to the
printer as data.

**(b) Drop LST: and drive the ports.**

| Hook | Code | Size |
|---|---|---|
| `ULOSTA` | `in a,(05h) / ret` (`DB 05 C9`) | 3 bytes, in place |
| `ULSTO` | `push af / L: in a,(05h) / inc a / jp nz,L / pop af / out (06h),a / ret` | 11 bytes in `MORPAT` |
| `ULPORT` | `ld a,02h / out (05h),a`, then 03h for PRTNAME `PS`, else 04h; `ret` | ~15-25 bytes |
| `ULUNPT` | `ld a,01h / out (05h),a / ret` | 5 bytes |

`ULSTO` could be just `out (06h),a / ret` in place, since the drivers
check status first. But `LSTSTR` does not check, and the port drops a
byte written while busy. The wait costs nothing when the printer is
ready.

This gains:

- **WordStar's printing no longer depends on the BIOS.** IOBYTE,
  `LSTDEV`, CP/M 3's `DEVICE` and the XIOS do not matter to it. Under
  MP/M the XIOS `LISTST` is a bare `RET` ("not used by MP/M 2"), so
  WordStar's status check there reads whatever is in A. `in a,(05h)`
  gives the true status.
- Background printing behaves as before: `ULOSTA` returns busy at once,
  and WordStar goes back to editing while a job opens (2-10 s).
- **Text or raw is chosen for each job from the printer's name** (with
  firmware 03h/04h), so the panel's "IPP sends" no longer has to be
  changed by hand between `PS` and `ASCII`.

And costs:

- WordStar can print only to this printer: it ignores `LSTDEV` and LST:
  redirection, so it cannot print to AUX: or a CP/M 3 `DEVICE` target.
  Printing to a file is unaffected, because `LO` handles it before
  `ULSTO`.
- More of the user area becomes specific to this machine (it already is,
  through `ws4_patch.py`).
- MP/M: two WordStars could write to the ports at the same time.
  WordStar still attaches and detaches LST: around a job when MPMFLG bit
  2 is set (that code runs whatever the hooks are), so set bit 2 in the
  MP/M build (see [wordstar-mpm.md](wordstar-mpm.md)).

Neither way changes the modem side. A prompt printout still needs
framing (parts 1 and 2), and PIP and the other programs still rely on
the idle time or the warm-boot end.

Users of an unpatched WordStar can make either change with WSCHANGE's
patcher, by label (`ULPORT`, `ULUNPT`, `ULOSTA`, `ULSTO`, `MORPAT`).

## Caveats

- **LST: redirected elsewhere** (in way (a)): the OUTs bypass the
  redirection. With no job running they do nothing, but a `02h` hint
  could hold a later job from another program. The cap and the next
  `01h` limit the damage, or PRTBEGIN can skip the hint unless LSTDEV is
  FFh.
- **MP/M:** `ULUNPT` runs before WordStar's detach, so it ends
  WordStar's own job, but only if WordStar attached LST: (MPMFLG bit 2).
  Without the attach, `01h` could cut another console's job.
- **A ^U abort** ends the job with whatever was sent. A PostScript job
  cut short may print an error page. IPP job cancellation is out of
  scope.
- **The next print straight after an end** finds the port busy during
  `CLOSE_WAIT`. With framing that is short, without it it lasts S61.
  WordStar treats it as a busy printer (`PRNDLY`, then "Printer may not
  be ready").

## Order of work (each step useful on its own)

1. `ws4_patch.py`: way (a)'s `ULUNPT`, or way (b)'s `ULOSTA`, `ULSTO`
   and `ULUNPT`. No firmware change. Correct job boundaries.
2. Firmware: `05h` commands 02h (held job) and 03h/04h (raw/text), plus
   the `ULPORT` patch. No more split jobs, and the right format for each
   job.
3. Zimodem fork: S64 framed mode, the `DLE ETX` end, `+++` off.
4. Firmware: the framed-mode setting, DLE escaping, `DLE ETX` at the
   end. Printouts come out at once.
5. Optional: `WBOOT` sends `01h`. Docs throughout.

Tests: printjob host tests (escaping, `DLE ETX` on `printjob_end`, the
hold's cap, `02h` before the first byte, 03h/04h). Zimodem by hand
against a CUPS queue: a 3-page WordStar `PS` print with pauses between
pages comes out as one job, as soon as the last page is sent.
