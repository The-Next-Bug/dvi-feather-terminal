#!/usr/bin/env bash
# The F83 disk's F83.COM and VIEWDRV.BLK (bd xdu): VIEWDRV.BLK, made from
# viewdrv.fth (blocks of 16 lines of at most 64 columns, separated by
# "---" lines), patches F83 so that VIEW reads the system's source from the
# drive F83.COM was loaded from (CP/M 3's 0050H). The original F83.COM
# (lib/f83) loads it and saves itself, under CP/M 2.2 on the Z80 machine:
# cpmrun (src/machine/cpm/cpm3/build.sh builds it) boots picosim's
# cpm22.dsk and types f83.run.
#
#   CPMRUN=... build.sh OUTDIR
#
# Also AY.BLK, the AY-3-8910 words (bd 1ls.18.7), from ay.fth: only put
# on the disk, not loaded.
#
# A .fth file: blocks of 16 lines of at most 63 columns (a block has no
# line ends: a 64th column runs into the next line's first word; a \
# comment line, such as F83's title lines, may take all 64),
# separated by "---" lines; a "===" line ends the code screens and starts
# their shadows, F83's comments for screen n at n + half the file (both
# halves padded to the same length).
#
# Writes OUTDIR/f83.com, OUTDIR/viewdrv.blk, OUTDIR/ay.blk and the console
# log OUTDIR/f83.log. Run by src/disks/diskimg.cmake (PYTHON: the
# interpreter for diskimg_gen.py).
set -euo pipefail

out="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$here/../../../.."
disks="$root/lib/z80pack/picosim/disks"
edit() { "${PYTHON:-python3}" "$root/src/disks/diskimg_gen.py" --edit "$@"; }
work="$out/f83work"
mkdir -p "$work"

# SOURCE.fth -> SOURCE.blk, for each argument pair.
"${PYTHON:-python3}" - "$here/viewdrv.fth" "$out/viewdrv.blk" "$here/ay.fth" "$out/ay.blk" <<'PY'
import sys
for src, dst in zip(sys.argv[1::2], sys.argv[2::2]):
    halves = [h.strip("\n").split("\n---\n") for h in open(src).read().split("\n===\n")]
    if len(halves) == 2:
        n = max(len(h) for h in halves)
        halves = [h + [""] * (n - len(h)) for h in halves]
    data = b""
    for n, block in enumerate(sum(halves, [])):
        lines = block.split("\n")
        if len(lines) > 16 or any(len(l) > (64 if l.startswith("\\") else 63) for l in lines):
            sys.exit(f"{src}: block {n} is over 16 lines of 63 columns")
        data += b"".join(l.ljust(64).encode("ascii") for l in lines).ljust(1024)
    open(dst, "wb").write(data)
PY

cp "$disks/cpm22.dsk" "$work/a.dsk"
cp "$disks/empty.dsk" "$work/b.dsk"
edit "$work/b.dsk" --file F83.COM "$root/lib/f83/F83.COM" --file VIEWDRV.BLK "$out/viewdrv.blk"
if ! "$CPMRUN" "$here/f83.run" "$work/a.dsk" "$work/b.dsk" > "$out/f83.log" 2> "$work/cpmrun.err"; then
  cat "$out/f83.log" "$work/cpmrun.err" >&2
  exit 1
fi
edit "$work/b.dsk" --extract F83.COM "$out/f83.com"
