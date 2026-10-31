# Idea: WordStar 4 under MP/M II, one copy per console

Status: speculation, not planned, no bd ticket. Written 2026-10-31 from
the WordStar 4 reconstruction (~/fz80/wordstar-4: its wiki's memory map,
the user area, WSCHANGE's script) and a run of MEM.COM under MP/M in the
host tests; nothing has been tried on the board or in WordStar's oracle.

The stock WS.COM starts under our MP/M II but cannot open a document
("Not enough memory to continue"). Two WSCHANGE choices, no code, should
fix that: install it as "Multi-user MP/M", then "Minimize memory
allocations". A copy then has about 37 records of text space against
the 28 it needs, and a second copy can run on the other console in a
bank of its own.

## Why documents don't load today

Each MP/M user segment is the bank-switched 0000H-BFFFH, so a program's
TPA ends at BFFCH (MEM: `TPA 0100H-BFFCH: 48893 bytes (47K)`).
WordStar's code and overlays take fixed addresses up to A480H (EDBASE);
the editor's memory has to fit above that:

| Above A480H, stock settings | Bytes |
|---|---|
| Room up to the BDOS (BFFDH) | about 7040 |
| Shorthand (HANMAX 4 records + 6) | 518 |
| Screen tables, status lines, resident messages | 1499 |
| Unerase (UNSIZE) + dot stack (DSTKSZ) | 1000 |
| Edit buffer (BFSIZE 16 records) | 2048 |
| Left for text | about 1970 (15 records) |
| Needed (EDSIZE) | 3584 (28 records) |

The message window (VMSIZE 4) fits the user area's patch space
(EXTRA..endpat, exactly 512 bytes) and costs nothing. CP/M 2.2 (BDOS at
E406H) gives about 9 KB more, which is why WordStar works there.

The platform cannot give back the difference: MP/M's shared code (XDOS
2200H, RESBDOS 0D00H, the XIOS's common part, the RSPs, the system
tables) fills all of C000H-FFFFH, and the MMU's common base is fixed at
C000H. Even a configurable split would gain at most about 2 KB.

## Settings to change

The values are the ones src/install/WSCHANGE.script writes; together
they free about 2.9 KB, leaving about 4800 bytes for text.

| Setting | Stock | After | What it does |
|---|---|---|---|
| MPMFLG | 00h | 2Fh | MP/M support; shared files (overlays opened read-only, f6'; documents unlocked, f5'; `$A$`/`$B$` checked); printer attach/detach; dispatch calls (BDOS 142) while waiting for keys |
| SHARE | 1 | your answer | a second user opening the same document: Y view only, N refused |
| ULOSTA | NOP NOP RET | OR 0FFh, RET | printer always ready (MP/M's attach does the waiting) |
| INITID | | "Multi-User" | shown at sign-on |
| HANMAX | 4 records | 1 | shorthand buffer: saves 384 bytes, only a few macros |
| BFSIZE (Edit) | 16 records | 3 | general-purpose edit buffer: saves 1664 bytes; block reads and writes in smaller pieces |
| DSTKSZ | 500 bytes | 75 | dot-command stack: saves 425 bytes, fewer nested .RR/.LM/.RM |
| UNSIZE | 500 bytes | 100 | unerase: saves 400 bytes, only short deletions come back |

The Opening Menu's buffer (BFSIZE+1) stays at 16: the Opening Menu has
room from 8680H, so its directory listing is not cut down. With so
little text in memory WordStar uses `$A$`/`$B$` often, which is cheap on
the SD card or the RAM disk.

## Applying them with WSCHANGE

Run WSCHANGE under CP/M 2.2 (the CPM22 set) on a copy of WS.COM and keep
the CP/M copy as it is; the minimized buffers only slow it down there.

1. Put WordStar's files and WSCHANGE on a mounted disk, e.g. B:, and
   boot CP/M 2.2.
2. `WSCHANGE`, with WS.COM as the file to install and an output name
   such as WSMPM.COM, so the original stays.
3. Main Installation Menu: C Computer, B Operating system, B Multi-user
   MP/M.
4. "If 2 people try to edit the same document...": Y lets the second one
   view it, N refuses them. After N, WSCHANGE goes on to another menu: X
   back out.
5. C Computer, C Memory usage, K Minimize memory allocations, Y.
6. Check the Memory usage menu: HANMAX 1, Edit buffer 3, dot command
   buffer 75, unerase 100.
7. X back to the Main Installation Menu, X again, save the changes.
8. Copy WSMPM.COM with WS.OVR, WSMSGS.OVR, WSHELP.OVR, WSPRINT.OVR and
   WSSHORT.OVR to the MP/M work disk (MPM2, or a disk mounted next to
   it), all in the same drive and user: WordStar looks for its overlays
   on its start-up drive and user first.

## Running one copy

Boot MP/M (MPM1 on A:, MPM2 on B:), go to WordStar's drive and start it
on the screen's console.

1. `MEM`: it should say `TPA 0100H-BFFCH`. A smaller TPA leaves less
   than the figures here.
2. `WSMPM`: the sign-on should say "Multi-User".
3. Open a document of more than 5 KB, so part of it goes out to `$A$`.
   Go to the end and back (^QC, ^QR), save with ^KD.
4. A block move (^KB, ^KK, ^KV) of a few KB and a find and replace (^QA)
   over the whole file: the commands that use the small buffers.
5. `DIR` afterwards: no `$A$` or `$B$` files left behind.

Still "Not enough memory to continue": EDSIZE (Memory usage B, 28
records) is the minimum check itself. Lowering it is possible but
riskier; its note says it is sized for an average full page.

## Running a second copy

A second copy runs on console 1, the board's serial port, in its own
bank, so it needs no more memory than the first. The PSRAM builds
(Frankless, the PSRAM Feathers) have 7 user banks; the SRAM boards have
2 usable ones (their third segment, trimmed into bank 0, is too small:
"No room").

1. A terminal (or tools/hostbridge.py) on the serial port at SET-UP's
   baud rate. It has to be the terminal WordStar is installed for;
   another one needs a second install, e.g. WSMPM2.COM.
2. Console 1 starts in user 1: `USER 0`, or copy the files there.
3. WordStar on both consoles, each on a different document. Without
   MPMFLG bit 1 the second would most likely stop at "WS.OVR?": the
   first holds the overlays open in MP/M's default locked mode.
4. The same document on both: the second gets the SHARE answer (view
   only, or "That file is already being edited").
5. Print from one copy while the other is idle; MP/M's printer attach
   should make a second print wait for the first.

Our GENSYS limits (16 open files a process, 32 for the system) are
enough for two copies at about 7-8 files each.

## Things to avoid, open questions

- **R (run a program) and S (spelling check):** not under MP/M. The run
  stub repoints the BIOS warm-boot jump and copies the BDOS entry page;
  under MP/M that jump table is most likely shared memory, so one copy's
  R could break the other, and a warm boot ends the MP/M process anyway.
- **Shorthand edits:** WSSHORT.OVR is opened for writing back (without
  f6'), so two copies saving shorthand changes will clash.
- **Not tested:** WordStar calls the BIOS console entries directly. On
  console 0 that works; whether the second copy's output reaches console
  1 through MP/M's XIOS jump table has to be checked on the board.
- **Not traced:** the memory figures come from the wiki, not from
  DOCINIT run in the oracle with the BDOS at BFFDH; a run there would
  confirm the 37 records first.
- **If the minimized buffers are too small:** a reassembled MP/M build
  of WordStar that moves EDBASE down (a smaller overlay C), giving up
  the byte-for-byte match with the original.
