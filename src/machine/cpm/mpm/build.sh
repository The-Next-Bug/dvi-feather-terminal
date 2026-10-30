#!/usr/bin/env bash
# MP/M II's MPM.SYS with our XIOS (bnkxios.mac: drives A-H of any
# format, bd l1j.2) for a machine with BANKS user banks, the way picosim's
# SYSGEN.SUB does it, by running M80, LINK and GENSYS under CP/M 2.2 on
# the Z80 machine: cpmrun (src/machine/cpm/cpm3/build.sh builds it) boots
# picosim's mpm-1.dsk (CP/M 2.2 on its system tracks) with mpm-2.dsk, the
# MP/M work disk, on B: and types the steps, GENSYS's answers included.
#
#   CPMRUN=... Z80ASM=... build.sh OUTDIR BANKS
#
# BANKS (1-7) is the number of 48 KB banks besides bank 0 the system
# gets, one user memory segment each (0000H-BFFFH; MP/M II has at most
# eight segments, the system's counted): 7 on the PSRAM boards, the
# firmware's MACHINE_BANKS (2) on the others, which also get bank 0
# below the system as a smaller segment (GENSYS trims it to fit).
#
# Writes OUTDIR/mpm.sys, OUTDIR/mpm1.dsk (picosim's mpm-1.dsk booting MP/M
# itself: picosim's boot loader and MPMLDR, with its LDRBIOS, on the
# system tracks, as picosim's putsys.c writes them, and this MPM.SYS),
# OUTDIR/mpm2.dsk (the work disk with our XIOS source and what GENSYS
# made, SYSTEM.DAT included, so its SYSGEN.SUB makes the same system
# again), the script OUTDIR/gensys.run and the console log
# OUTDIR/gensys.log. Fails if a step reports an error or the XIOS's
# common part starts below the common base. Run by
# src/disks/diskimg.cmake (PYTHON: the interpreter for diskimg_gen.py;
# Z80ASM: z80pack's z80asm, built by src/machine/cpm/build.sh).
set -euo pipefail

out="$1"
banks="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$here/../../../.."
z80pack="$root/lib/z80pack"
disks="$z80pack/picosim/disks"
srcmpm="$z80pack/picosim/srcmpm"
edit() { "${PYTHON:-python3}" "$root/src/disks/diskimg_gen.py" --edit "$@"; }
work="$out/mpmwork"
mkdir -p "$work"
[[ "$banks" =~ ^[1-7]$ ]] || { echo "build.sh: BANKS must be 1-7, not $banks" >&2; exit 1; }

# CP/M text files end their lines in CR LF.
sed 's/$/\r/' "$here/bnkxios.mac" > "$work/bnkxios.mac"

cp "$disks/mpm-1.dsk" "$work/a.dsk"
cp "$disks/mpm-2.dsk" "$work/b.dsk"
# Off with picosim's XIOS and everything made from it (SYSTEM.DAT holds
# picosim's answers; without it GENSYS asks every question), so nothing
# stale is left if a step makes nothing.
edit "$work/b.dsk" --delete BNKXIOS.MAC --delete BNKXIOS.SPR --delete MPM.SYS --delete SYSTEM.DAT \
  --file BNKXIOS.MAC "$work/bnkxios.mac"

# GENSYS's questions, in its order. Defaults kept but for: the top page
# (FE, as picosim's: FF00H is not free), two consoles (the XIOS has
# two: the screen and the serial port), no dayfile (a time stamp before
# every command's output), every resident process (HWCLOCK sets the
# time of day from the clock ports, bd l1j.6), and the memory segments.
{
  cat <<'EOF'
# Made by src/machine/cpm/mpm/build.sh: M80, LINK and GENSYS on the work
# disk in B:, LINK on A:.
! UNDEFINED
! NO FILE
! Entry error
! cannot trim
> A>
< B:
> B>
< M80 =BNKXIOS/M
> B>
< A:LINK BNKXIOS[OS]
> B>
< ERA BNKXIOS.SYM
> B>
< ERA BNKXIOS.REL
> B>
< GENSYS
> page of operating system (FF) ?
< FE
> (system consoles) (#4) ?
< #2
EOF
  for q in "Printers (#1) ? " "RST (06) ? " "user stacks (Y) ? " "CPU (Y) ? " "second (#60) ? " \
      "System Drive (A:) ? " "file drive (A:) ? " "records/process (#16) ? " "records/system (#32) ? " \
      "files/process (#16) ? " "files/system (#32) ? " "Bank switched memory (Y) ? "; do
    printf '> %s\n<\n' "$q"
  done
  segments=$banks
  (( banks < 7 )) && segments=$((banks + 1))
  printf '> memory segments (#3) ? \n< #%s\n' "$segments"
  printf '> base page (C0) ? \n<\n> at console (Y) ? \n< N\n> page entries (Y) ? \n<\n'
  for rsp in SPOOL ABORT SCHED MPMSTAT HWCLOCK; do
    printf '> %s\n> RSP (N) ? \n< Y\n' "$rsp"
  done
  printf '> attrib,bank\n<\n' # the system's own segment
  for ((b = 1; b <= banks; ++b)); do
    printf '> attrib,bank\n< 00,C0,00,%02X\n' "$b"
  done
  if (( banks < 7 )); then
    printf '> attrib,bank\n< 00,C0,00,00\n> segment trimmed\n> attrib,bank\n<\n'
  fi
  printf '> segment table entries (Y) ? \n<\n> GENSYS DONE\n> B>\n'
} > "$out/gensys.run"

if ! "${CPMRUN:?}" "$out/gensys.run" "$work/a.dsk" "$work/b.dsk" > "$out/gensys.log" 2> "$work/cpmrun.err"; then
  cat "$out/gensys.log" "$work/cpmrun.err" >&2
  exit 1
fi
if tr -d '\r' < "$out/gensys.log" | grep -E '^ ?[1-9][0-9]* Fatal error' >&2; then
  echo "M80 reported errors; log: $out/gensys.log" >&2
  exit 1
fi

edit "$work/b.dsk" --extract MPM.SYS "$out/mpm.sys" --extract BNKXIOS.SPR "$work/bnkxios.spr"

# The XIOS's common part (COMMONBASE, its first jump's target) must be
# above the common base, C000H: it runs with any bank selected.
"${PYTHON:-python3}" - "$out/gensys.log" "$work/bnkxios.spr" <<'PY'
import re, sys
log = open(sys.argv[1], 'rb').read().decode('ascii', 'replace')
at = re.findall(r'BNKXIOS SPR  ([0-9A-F]{4})H', log)
if not at:
    sys.exit('GENSYS placed no BNKXIOS.SPR')
spr = open(sys.argv[2], 'rb').read()
if spr[256] != 0xC3:
    sys.exit('BNKXIOS.SPR does not start with a jump')
common = int(at[-1], 16) + spr[257] + 256 * spr[258]
if common < 0xC000:
    sys.exit('the XIOS common part starts at %04XH, below the common base C000H' % common)
PY

# MPM1: picosim's boot loader (track 0 sector 1: it loads 51 sectors to
# 0100H and jumps there) and MPMLDR with its LDRBIOS from sector 2 on.
(cd "$work" && "${Z80ASM:?}" -T -sn -p0 -fb -l"$work/boot.lis" -o"$work/boot.bin" "$srcmpm/boot.asm") > "$work/z80asm.log" 2>&1 ||
  { cat "$work/z80asm.log" >&2; exit 1; }
cp "$disks/mpm-1.dsk" "$out/mpm1.dsk"
"${PYTHON:-python3}" - "$out/mpm1.dsk" "$work/boot.bin" "$srcmpm/mpmldr.bin" <<'PY'
import sys
image, boot, ldr = sys.argv[1], open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read()
if len(boot) > 128 or len(ldr) > 51 * 128:
    sys.exit('the boot loader or MPMLDR does not fit the system tracks')
ldr += bytes(-len(ldr) % 128)
data = bytearray(open(image, 'rb').read())
data[0:128] = boot + bytes(128 - len(boot))
data[128:128 + len(ldr)] = ldr
open(image, 'wb').write(data)
PY
edit "$out/mpm1.dsk" --delete MPM.SYS --file MPM.SYS "$out/mpm.sys"
cp "$work/b.dsk" "$out/mpm2.dsk"
