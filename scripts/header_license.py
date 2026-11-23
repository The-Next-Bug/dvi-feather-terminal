#!/usr/bin/env python3
"""Print the license in a C header's comment block, without the comment marks.

Usage: header_license.py HEADER MARKER [MARKER...]

The block is the first /* ... */ comment holding every MARKER (e.g. the
copyright holder and a phrase of the license), so a header whose license
is not where it was cannot pass for one. Lines lose their leading space and
one '*' or '/' (FatFs's ff.h frames its comment with '/') and a space.
Used by scripts/build-zimodem.sh for license texts that are only in the
toolchain's headers.
"""
import re
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    path, markers = sys.argv[1], sys.argv[2:]
    text = open(path, encoding="utf-8", errors="replace").read()
    for block in re.findall(r"/\*(.*?)\*/", text, re.S):
        if all(m in block for m in markers):
            lines = [re.sub(r"^\s*[*/]? ?", "", line).rstrip() for line in block.splitlines()]
            while lines and not lines[0].strip(" -*/"):
                lines.pop(0)
            while lines and not lines[-1].strip(" -*/"):
                lines.pop()
            print("\n".join(lines))
            return
    sys.exit(f"{path}: no comment block with {markers}")


if __name__ == "__main__":
    main()
