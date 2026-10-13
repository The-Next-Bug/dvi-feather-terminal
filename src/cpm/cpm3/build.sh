#!/usr/bin/env bash
# Make CP/M 3's CPM3.SYS with our banked BIOS (bnkbios3.asm, gencpm.dat)
# the way picosim's SYSGEN.SUB does, by running DRI's RMAC, LINK and
# GENCPM under CP/M 3 on the Z80 machine: cpmrun (cpmrun.c with
# src/machine.c and z80pack's CPU, built here with the host C compiler,
# CC, default cc) boots picosim's cpm3-1.dsk and types sysgen.run.
#
#   build.sh OUTDIR
#
# Writes OUTDIR/cpm3.sys and OUTDIR/cpm3gen.dsk (picosim's cpm3gen.dsk
# with our sources and this build's RELs, SPRs and CPM3.SYS: the same
# SYSGEN.SUB run on the machine makes the same system), and the console
# log OUTDIR/sysgen.log. Fails if a step reports an error or the
# common part of the system grows (the TPA must stay 61K). Run by
# cmake/diskimg.cmake (PYTHON: the interpreter for diskimg_gen.py).
set -euo pipefail

out="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$here/../../.."
z80pack="$root/lib/z80pack"
disks="$z80pack/picosim/disks"
edit() { "${PYTHON:-python3}" "$root/src/diskimg_gen.py" --edit "$@"; }
work="$out/cpm3work"
mkdir -p "$work"

"${CC:-cc}" -O2 -w -o "$out/cpmrun" -I"$root/src" -I"$root/src/z80pack" \
  -I"$z80pack/z80core" -I"$z80pack/iodevices" "$here/cpmrun.c" "$root/src/machine.c" \
  "$z80pack"/z80core/sim{glb,core,z80,z80-cb,z80-dd,z80-ddcb,z80-ed,z80-fd,z80-fdcb}.c \
  "$z80pack/iodevices/sd-fdc.c"

# CP/M text files end their lines in CR LF.
sed 's/$/\r/' "$here/bnkbios3.asm" > "$work/bnkbios3.asm"
sed 's/$/\r/' "$here/gencpm.dat" > "$work/gencpm.dat"

cp "$disks/cpm3-1.dsk" "$work/a.dsk"
cp "$disks/cpm3gen.dsk" "$work/b.dsk"
cp "$disks/cpm3-2.dsk" "$work/c.dsk"
# Off with picosim's BIOS and everything made from it, so nothing stale
# is left if a step makes nothing.
edit "$work/b.dsk" --delete BNKBIOS3.ASM --delete GENCPM.DAT \
  --delete BNKBIOS3.REL --delete BNKBIOS3.SPR --delete SCB.REL --delete CPM3.SYS \
  --file BNKBIOS3.ASM "$work/bnkbios3.asm" --file GENCPM.DAT "$work/gencpm.dat"

if ! "$out/cpmrun" "$here/sysgen.run" "$work/a.dsk" "$work/b.dsk" "$work/c.dsk" \
    > "$out/sysgen.log" 2> "$work/cpmrun.err"; then
  cat "$out/sysgen.log" "$work/cpmrun.err" >&2
  exit 1
fi
# RMAC lists a line with an error with its letter in column 1, then the
# address (U0218 C30000  JMP NOWHERE).
if tr -d '\r' < "$out/sysgen.log" | grep -E '^[A-Z][0-9A-F]{4} ' >&2; then
  echo "RMAC reported errors (the letter before each line above); log: $out/sysgen.log" >&2
  exit 1
fi
# GENCPM's map: the TPA ends at RESBDOS3, at F500H in picosim's system
# (61K); no lower.
resbdos=$(sed -n 's/^ RESBDOS3 SPR  \([0-9A-F]\{4\}\)H.*/\1/p' "$out/sysgen.log" | tail -1)
[[ -n "$resbdos" ]] && (( 16#$resbdos >= 16#F500 )) ||
  { grep "SPR" "$out/sysgen.log" >&2; echo "CP/M 3's TPA is below 61K" >&2; exit 1; }

edit "$work/b.dsk" --extract CPM3.SYS "$out/cpm3.sys"
cp "$work/b.dsk" "$out/cpm3gen.dsk"
