#!/usr/bin/env bash
# Assemble the CP/M 2.2 boot loader and BIOS (boot.asm, bios.asm) into
# OUTDIR/boot.bin and OUTDIR/bios.bin with z80pack's z80asm, built here from
# lib/z80pack/z80asm with the host C compiler (CC, default cc). Checks that
# they fit the system tracks: the loader one sector, the BIOS's code and
# tables the 7 sectors after CCP and BDOS, its data area below the boot ROM
# page (FF00H). Run by cmake/diskimg.cmake.
set -euo pipefail

out="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
z80asm_src="$here/../../lib/z80pack/z80asm"
mkdir -p "$out"

if [[ ! -x "$out/z80asm" || "$out/z80asm" -ot "$z80asm_src/z80asm.c" ]]; then
  "${CC:-cc}" -O2 -w -o "$out/z80asm" "$z80asm_src"/*.c
fi

asm() { # source binary listing
  "$out/z80asm" -8 -fb -x -sn -p0 -l"$3" -o"$2" "$1" > "$out/z80asm.log" 2>&1 ||
    { cat "$out/z80asm.log" >&2; exit 1; }
  grep -q "^0 error(s)" "$out/z80asm.log" || { cat "$out/z80asm.log" >&2; exit 1; }
}
asm "$here/boot.asm" "$out/boot.bin" "$out/boot.lis"
asm "$here/bios.asm" "$out/bios.bin" "$out/bios.lis"

size() { stat -c %s "$1"; }
(( $(size "$out/boot.bin") <= 128 )) || { echo "boot.bin is over one sector" >&2; exit 1; }
(( $(size "$out/bios.bin") <= 7 * 128 )) || { echo "bios.bin is over 7 sectors" >&2; exit 1; }
enddat=$(sed -n 's/.*ENDDAT *\([0-9a-f]\{4\}\).*/\1/p' "$out/bios.lis" | tail -1)
[[ -n "$enddat" ]] || { echo "ENDDAT not in bios.lis" >&2; exit 1; }
(( 16#$enddat <= 16#ff00 )) || { echo "BIOS data ends at $enddat, in the boot ROM page" >&2; exit 1; }
