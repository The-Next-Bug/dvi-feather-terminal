#!/usr/bin/env python3
"""Write a WiFi modem image container, ZIMODEM.ZMI (bd xir.17.13).

Usage: zmi_gen.py --module NAME --version VERSION --flash-size SIZE OUT ADDRESS:FILE...

The format is in src/modem/zmi.h. SIZE is in bytes or with K/M (4M);
ADDRESS is where FILE goes in the module's flash (0x0 for the ESP8266's
image). Only the ESP8266 is known so far. scripts/build-zimodem.sh runs it.
"""

import argparse
import struct
import sys
import zlib

CHIPS = {"esp8266": 1}
SECTOR = 4096
# The ESP8266 image header's flash size code (byte 3, high nibble).
ESP8266_SIZES = {0: 0x80000, 1: 0x40000, 2: 0x100000, 3: 0x200000, 4: 0x400000, 5: 0x200000,
                 6: 0x400000, 8: 0x800000, 9: 0x1000000}


def size(text):
    mult = {"K": 1024, "M": 1024 * 1024}.get(text[-1:].upper(), 1)
    return int(text[:-1] if mult > 1 else text, 0) * mult


def text_field(text, n, what):
    data = text.encode("ascii")
    if not data or len(data) >= n or any(b < 0x20 or b > 0x7E for b in data):
        sys.exit(f"{what} must be 1-{n - 1} printable ASCII characters: {text!r}")
    return data.ljust(n, b"\0")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--chip", default="esp8266", choices=sorted(CHIPS))
    ap.add_argument("--module", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--flash-size", required=True, type=size)
    ap.add_argument("out")
    ap.add_argument("parts", nargs="+", metavar="ADDRESS:FILE")
    args = ap.parse_args()

    parts = []
    for spec in args.parts:
        address, _, path = spec.partition(":")
        with open(path, "rb") as f:
            parts.append((int(address, 0), f.read()))
    parts.sort()
    end = 0
    for address, data in parts:
        if address % SECTOR or address < end or not data or address + len(data) > args.flash_size:
            sys.exit(f"part at 0x{address:x} ({len(data)} bytes): unaligned, overlapping or past the flash")
        end = address + len(data)
    image = dict(parts).get(0)
    if image is None or len(image) < 4 or image[0] != 0xE9 or ESP8266_SIZES.get(image[3] >> 4) != args.flash_size:
        sys.exit("no ESP image at 0x0 for a flash of that size")

    body = b"".join(struct.pack("<II", a, len(d)) + d for a, d in parts)
    header = (b"ZMI1" + struct.pack("<HBB", 64, CHIPS[args.chip], 0) + text_field(args.module, 16, "module") +
              text_field(args.version, 24, "version") +
              struct.pack("<IIII", args.flash_size, len(parts), len(body), 0))
    assert len(header) == 64
    data = header + body
    with open(args.out, "wb") as f:
        f.write(data + struct.pack("<I", zlib.crc32(data)))


if __name__ == "__main__":
    main()
