#!/usr/bin/env python3
"""Patch WordStar 4.00 (lib/wordstar, bd 6on) for the WS4 disk.

Usage: ws4_patch.py WS.OVR WS.COM WS80.COM WSADM.COM OUTDIR

Writes the patched files to OUTDIR under their lower-case names. Every
patch checks the bytes it replaces first, so a WordStar other than the
one these were worked out on (the shipped 4.00 WS.OVR; the user areas of
the VT100-installed WS*.COM) stops the build instead of being broken.

Addresses are from the WordStar 4 reconstruction (~/fz80/wordstar-4,
docs/wiki/ref): WS.OVR's resident part runs at file offset + 2480h,
overlay C at file offset - 0A80h; WS.COM's user area at file offset +
100h.

WS.OVR: the temporary files on H:

WordStar keeps the text that does not fit in memory in NAME.$B$ (before
the cursor) and NAME.$A$ (after it), and a block move's in NAME.$C$, all
made by TEMPFCB on the document's drive. It has no setting for another
drive: "WS NAME d:" puts them on d: but also saves the document there
(the save renames $B$ to the document, and swaps the two FCBs' drives).
So:

1. TEMPFCB (3B0Dh): the drive is always H: (ld a,8 in place of "BFCB's
   drive if set, else the document's").
2. KCMD_SAVEDOC (8A8Eh): always the copy path (read $B$, write the
   document up to its ^Z), which writes FILE2FCB on the document's own
   drive, never the rename of $B$ (which would leave the document on H:).
   The rename's size check (8A91h-8AA8h) is skipped by this, and its
   bytes hold patch 4.
3. The copy itself (8ABBh-8B01h), rewritten: WordStar's read $B$ a
   buffer at a time from its random record, which still pointed past the
   end (only "stale records" past the end ever took it), and a short read
   at the end of the file counted as a failure. Ours starts both files at
   record 0 and copies a record at a time up to the one with the ^Z.
4. 8A91h, called from the copy's success (8B14h): BFCB's drive set to
   the document's before the drive swap at 8B45h, so the swap changes
   nothing. Only when the copy fails (the document's disk full) does the
   save fall back to renaming $B$ on H:, and then the swap points the
   document at H:, where its text now is (WordStar's own fallback).

Its addresses: BFCB 1C86h, FILE2FCB 1BF2h, DOCFCB 1BCCh, VARTAB (the
general buffer's address) 1B2Ah, CLRRAND 0FC2h, RDBLOCK 10ADh, WRBLOCK
10C8h (one record each: NZ if done), c_l8b02 (close both, delete $B$)
and c_l8b17 (failed: rename $B$ instead).

WS*.COM: overlays from C:

LGLDRV's first drive is where WordStar looks for its files (WS.OVR,
WSMSGS.OVR, ...) after the current drive. "CAB..." in place of "ABC..."
lets C:WS run from A: with the WS4 disk on C:, as the WS4 set mounts it.
"""

import os
import sys

TEMP_DRIVE = 8  # H:

OVR_PATCHES = [
    # (file offset, original bytes, new bytes, what)
    (0x168d, "3a 87 1c b7 c2 15 3b 7e", f"3e {TEMP_DRIVE:02x} 00 00 00 00 00 00",
     "TEMPFCB: temporary files on H:"),
    (0x950e, "3a 57 23", "c3 a9 8a",
     "KCMD_SAVEDOC: copy $B$ to the document, never rename it"),
    (0x953b,
     "cd bb 0a 2a 2a 1b eb 21 86 1c cd ad 10 ca 17 8b 3a 2c 1b 91 ca 02 8b 2a 2a 1b 2b 47"
     " 0e 00 16 80 0c 23 7e fe 1a ca eb 8a 15 c2 dc 8a 05 c2 d9 8a e5 2a 2a 1b eb 21 f2"
     " 1b 06 00 cd c8 10 e1 ca 17 8b 7e fe 1a c2 bb 8a",
     "21 86 1c cd c2 0f"        # 8ABB ld hl,BFCB; call CLRRAND
     " 21 f2 1b cd c2 0f"       # 8AC1 ld hl,FILE2FCB; call CLRRAND
     " 2a 2a 1b eb 21 86 1c"    # 8AC7 loop: de = buffer, hl = BFCB
     " 01 01 00 cd ad 10"       # 8ACE one record: RDBLOCK
     " ca 17 8b"                # 8AD4 jp z,c_l8b17 (no ^Z before the end)
     " 2a 2a 1b eb 21 f2 1b"    # 8AD7 de = buffer, hl = FILE2FCB
     " 01 01 00 cd c8 10"       # 8ADE WRBLOCK
     " ca 17 8b"                # 8AE4 jp z,c_l8b17 (disk full)
     " 2a 2a 1b 06 80"          # 8AE7 hl = buffer, b = 128
     " 7e fe 1a ca 02 8b"       # 8AEC scan: a ^Z: done, c_l8b02
     " 23 05 c2 ec 8a"          # 8AF2 next byte
     " c3 c7 8a"                # 8AF7 next record
     " 00 00 00 00 00 00 00 00",  # 8AFA to 8B01
     "the copy: from record 0, a record at a time"),
    (0x9511, "b7 c2 26 8b 2a a8 1c 11 04", "3a cd 1b 32 87 1c c3 2f 8b",
     "8A91h: BFCB's drive = DOCFCB's, then on to 8B2Fh"),
    (0x9594, "c3 2f 8b", "c3 91 8a",
     "the copy's success goes through 8A91h"),
]

COM_PATCHES = [
    (0x05e3, b"ABCDEFGHIJKLMNOP\0".hex(" "), b"CABDEFGHIJKLMNOP\0".hex(" "),
     "LGLDRV: WordStar's files on C:"),
]


def patch(path, patches, outdir):
    data = bytearray(open(path, "rb").read())
    for offset, old, new, what in patches:
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        assert len(old) == len(new)
        if data[offset:offset + len(old)] != old:
            sys.exit(f"{path}: not the WordStar these patches are for "
                     f"(at {offset:04x}h, for {what}: {data[offset:offset + len(old)].hex(' ')})")
        data[offset:offset + len(new)] = new
    out = os.path.join(outdir, os.path.basename(path).lower())
    with open(out, "wb") as f:
        f.write(data)


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__.split("\n\n")[1])
    ovr, coms, outdir = sys.argv[1], sys.argv[2:5], sys.argv[5]
    os.makedirs(outdir, exist_ok=True)
    patch(ovr, OVR_PATCHES, outdir)
    for com in coms:
        patch(com, COM_PATCHES, outdir)


if __name__ == "__main__":
    main()
