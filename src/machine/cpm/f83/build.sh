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
# Writes OUTDIR/f83.com, OUTDIR/viewdrv.blk and the console log
# OUTDIR/f83.log. Run by src/disks/diskimg.cmake (PYTHON: the
# interpreter for diskimg_gen.py).
set -euo pipefail

out="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$here/../../../.."
disks="$root/lib/z80pack/picosim/disks"
edit() { "${PYTHON:-python3}" "$root/src/disks/diskimg_gen.py" --edit "$@"; }
work="$out/f83work"
mkdir -p "$work"

"${PYTHON:-python3}" - "$here/viewdrv.fth" "$out/viewdrv.blk" <<'PY'
import sys
blocks = open(sys.argv[1]).read().rstrip("\n").split("\n---\n")
data = b""
for n, block in enumerate(blocks):
    lines = block.split("\n")
    if len(lines) > 16 or any(len(l) > 64 for l in lines):
        sys.exit(f"{sys.argv[1]}: block {n} is over 16 lines of 64 columns")
    data += b"".join(l.ljust(64).encode("ascii") for l in lines).ljust(1024)
open(sys.argv[2], "wb").write(data)
PY

cp "$disks/cpm22.dsk" "$work/a.dsk"
cp "$disks/empty.dsk" "$work/b.dsk"
edit "$work/b.dsk" --file F83.COM "$root/lib/f83/F83.COM" --file VIEWDRV.BLK "$out/viewdrv.blk"
if ! "$CPMRUN" "$here/f83.run" "$work/a.dsk" "$work/b.dsk" > "$out/f83.log" 2> "$work/cpmrun.err"; then
  cat "$out/f83.log" "$work/cpmrun.err" >&2
  exit 1
fi
edit "$work/b.dsk" --extract F83.COM "$out/f83.com"
