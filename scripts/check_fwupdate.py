#!/usr/bin/env python3
"""Check that the firmware update's commit (src/fwupdate.c) cannot read the
flash it rewrites.

From the disassembly (dvi_terminal.dis), follows every direct call and
branch from fwupdate_commit_image and fails if any function reached is not
in SRAM or holds a literal pointing into the flash window (0x10000000 -
0x10ffffff: string literals, const tables, code left in flash). Indirect
calls (the boot ROM's functions) cannot be followed; they are listed.

Also checks, from the link map, that the image ends below the staging area.

Usage: check_fwupdate.py <build dir> [staging offset, default 0x200000]
"""
import re
import sys

ROOT = 'fwupdate_commit_image'
FLASH_LO, FLASH_HI = 0x10000000, 0x11000000

FUNC = re.compile(r'^([0-9a-f]{8}) <([^>]+)>:$')
INSN = re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s)+\s*(\S+)\s*(.*)$')
TARGET = re.compile(r'\b[0-9a-f]+ <([^>+]+)(?:\+0x[0-9a-f]+)?>')
WORD = re.compile(r'\.word\s+0x([0-9a-f]{8})')


def functions(path):
    funcs, name = {}, None
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            m = FUNC.match(line)
            if m:
                name = m.group(2)
                funcs.setdefault(name, (int(m.group(1), 16), []))
            elif name and line.strip():
                funcs[name][1].append(line)
    return funcs


def main():
    build = sys.argv[1]
    staging = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x200000
    funcs = functions(f'{build}/dvi_terminal.dis')
    errors, indirect = [], []
    if ROOT not in funcs:
        sys.exit(f'check_fwupdate: {ROOT} not found in the disassembly')
    seen, todo = set(), [ROOT]
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.add(name)
        if name not in funcs:
            errors.append(f'{name}: not in the disassembly')
            continue
        addr, lines = funcs[name]
        if not 0x20000000 <= addr < 0x20082000:
            errors.append(f'{name}: at {addr:#010x}, not in SRAM')
        for line in lines:
            m = INSN.match(line)
            if not m:
                continue
            op, args = m.group(2), m.group(3)
            w = WORD.search(line)
            if w and FLASH_LO <= int(w.group(1), 16) < FLASH_HI:
                errors.append(f'{name}: literal 0x{w.group(1)} points into flash ({line.strip()})')
            if op.startswith(('b', 'cb')):
                t = TARGET.search(args)
                if t and t.group(1) != name:
                    todo.append(t.group(1))
                elif op.startswith('blx') and not t:
                    indirect.append(f'{name}: {line.strip()}')

    end = None
    with open(f'{build}/dvi_terminal.elf.map') as f:
        for line in f:
            m = re.match(r'\s+0x([0-9a-f]+)\s+(?:PROVIDE \()?__flash_binary_end\b', line)
            if m:
                end = int(m.group(1), 16)
    if end is None:
        errors.append('__flash_binary_end not in the link map')
    elif end - FLASH_LO > staging:
        errors.append(f'the image ends at {end:#x}, past the staging area at {FLASH_LO + staging:#x}')

    print(f'check_fwupdate: {len(seen)} functions from {ROOT}: {", ".join(sorted(seen))}')
    for i in indirect:
        print(f'check_fwupdate: indirect call (not followed): {i}')
    if end is not None:
        print(f'check_fwupdate: image ends at {end:#x}, staging area at {FLASH_LO + staging:#x}')
    for e in errors:
        print(f'check_fwupdate: error: {e}', file=sys.stderr)
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
