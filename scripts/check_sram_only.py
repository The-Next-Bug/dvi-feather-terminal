#!/usr/bin/env python3
"""Check that code which must never touch the flash cannot reach it.

Most code runs from SRAM (COPY_TO_RAM), but cold code and constant data
stay in flash and are read through XIP (ld/section_copy_to_ram_rodata.incl,
bd qur.24). These must never read the flash:
- the firmware update's commit (src/platform/fwupdate.c), which rewrites it;
- core 1 (video) and its DMA interrupt, which must not wait on XIP;
- video_init, which changes clk_sys, and whatever main() calls after it
  until nvflash_retime sets XIP up for the new clock;
- the nvflash operations, which turn XIP off while they run.

From the disassembly (dvi_terminal.dis), follows every direct call and
branch from each root and fails if any function reached is not in SRAM or
holds a literal pointing into the flash window (0x10000000 - 0x10ffffff:
string literals, const tables, code left in flash). Indirect calls (the
boot ROM's functions) cannot be followed; they are listed.

Also checks, from the link map, that the image ends below the staging area.

Usage: check_sram_only.py <build dir> [staging offset, default 0x200000]
"""
import re
import sys

ROOTS = ('fwupdate_commit_image', 'core1_main', 'dma_irq', 'video_init', 'nvflash_retime', 'nvflash_erase',
         'nvflash_program', 'nvflash_read', 'nvflash_write')
FLASH_LO, FLASH_HI = 0x10000000, 0x11000000
# Calls a function makes between two others are roots too: main() between
# video_init (the clock change) and nvflash_retime.
WINDOWS = (('main', 'video_init', 'nvflash_retime'),)
# Flash addresses that code only compares against, never reads or calls.
ADDRESS_ONLY = {
    '__unhandled_user_irq': 'hardware_irq checks a vector against the default handler',
}

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
    address_only = {funcs[n][0] | 1 for n in ADDRESS_ONLY if n in funcs} | \
        {funcs[n][0] for n in ADDRESS_ONLY if n in funcs}
    errors, indirect, reached = [], [], {}
    roots = list(ROOTS)
    for func, start, end in WINDOWS:
        calls = [t.group(1) for line in funcs.get(func, (0, []))[1]
                 for m in [INSN.match(line)] if m and m.group(2).startswith('bl')
                 for t in [TARGET.search(m.group(3))] if t]
        if start not in calls or end not in calls[calls.index(start):]:
            errors.append(f'{func}: no call to {start} followed by {end}')
            continue
        i = calls.index(start)
        roots += calls[i + 1:calls.index(end, i)]
    for root in roots:
        if root not in funcs:
            sys.exit(f'check_sram_only: {root} not found in the disassembly')
        seen, todo = set(), [root]
        while todo:
            name = todo.pop()
            if name in seen:
                continue
            seen.add(name)
            if name not in funcs:
                errors.append(f'{root}: {name}: not in the disassembly')
                continue
            addr, lines = funcs[name]
            if not 0x20000000 <= addr < 0x20082000:
                errors.append(f'{root}: {name}: at {addr:#010x}, not in SRAM')
            for line in lines:
                m = INSN.match(line)
                if not m:
                    continue
                op, args = m.group(2), m.group(3)
                w = WORD.search(line)
                if w and FLASH_LO <= int(w.group(1), 16) < FLASH_HI and int(w.group(1), 16) not in address_only:
                    errors.append(f'{root}: {name}: literal 0x{w.group(1)} points into flash ({line.strip()})')
                if op.startswith(('b', 'cb')):
                    t = TARGET.search(args)
                    if t and t.group(1) != name:
                        todo.append(t.group(1))
                    elif op.startswith('blx') and not t:
                        indirect.append(f'{name}: {line.strip()}')
        reached[root] = seen

    # Every pattern of the cold code's linker script must place something:
    # one that no longer matches (an object or archive renamed or moved)
    # silently puts that code back in SRAM.
    end, cold, pattern, in_cold = None, None, None, False
    with open(f'{build}/dvi_terminal.elf.map') as f:
        for line in f:
            m = re.match(r'\s+0x([0-9a-f]+)\s+(?:PROVIDE \()?__flash_binary_end\b', line)
            if m:
                end = int(m.group(1), 16)
            if re.search(r'\b__cold_text_start__ = ', line):
                cold, in_cold = {}, True
            elif re.search(r'\b__cold_text_end__ = ', line):
                in_cold = False
            elif in_cold:
                m = re.match(r'^ (\*\S+)\(', line)
                if m:
                    pattern = m.group(1)
                    cold[pattern] = 0
                elif pattern and line.startswith(' .'):
                    cold[pattern] += 1
    if not cold:
        errors.append('no cold code patterns between __cold_text_start__ and __cold_text_end__ in the link map')
    for p, n in (cold or {}).items():
        if not n:
            errors.append(f'cold code pattern {p} placed nothing (its code left in SRAM?)')
    if end is None:
        errors.append('__flash_binary_end not in the link map')
    elif end - FLASH_LO > staging:
        errors.append(f'the image ends at {end:#x}, past the staging area at {FLASH_LO + staging:#x}')

    for root, seen in reached.items():
        print(f'check_sram_only: {len(seen)} functions from {root}')
    for i in sorted(set(indirect)):
        print(f'check_sram_only: indirect call (not followed): {i}')
    if end is not None:
        print(f'check_sram_only: image ends at {end:#x}, staging area at {FLASH_LO + staging:#x}')
    if cold:
        print(f'check_sram_only: {len(cold)} cold code patterns, each placing something')
    for e in errors:
        print(f'check_sram_only: error: {e}', file=sys.stderr)
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
