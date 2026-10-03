#!/usr/bin/env python3
"""Compress CP/M disk images into a C source for the firmware (diskimg.h).

Usage: diskimg_gen.py OUTPUT.c [--system NAME BOOT BIOS CPMBIN RELOC MSIZE]
                        [--file NAME CPMNAME HOSTFILE ...]
                        NAME=IMAGE [NAME=IMAGE ...]

Each IMAGE is compressed as an LZ4 block with match offsets below
LZ_WINDOW (src/lz.h), which src/lz.c decodes a window at a time while the
image is written to the SD card as /DISKS80/NAME.DSK. Compiled with
DISKIMG_IN_FLASH=1 (firmware), the compressed data goes in a .flashdata
section, which the SDK's copy_to_ram link leaves in flash; the host tests
compile it as ordinary const data.

--system puts a CP/M 2.2 system on the system tracks of image NAME (an 8"
floppy: 128-byte sectors stored track by track): the boot loader BOOT in
sector 1, then the CCP and BDOS from CPMBIN (z80pack's 64K cpm.bin, CCP at
offset 0x880) moved to MSIZE K with the relocation bitmap RELOC (one bit
per byte, set where the byte is the high byte of an address in CCP/BDOS;
made by comparing with MOVCPM's 62K output, see src/cpm/README.md), then
the BIOS BIOS (src/cpm/build.sh).

--file (any number) adds HOSTFILE to the CP/M file system of image NAME (an
8" floppy, user 0) as CPMNAME (8.3, e.g. WORMS.COM); the name must not be
there already.
"""

import sys
import zlib

WINDOW = 4096           # LZ_WINDOW in src/lz.h
MAX_OFFSET = WINDOW - 1
MIN_MATCH = 4
CHAIN = 64              # candidates tried per position


def compress(data):
    """Greedy LZ4 block compression with hash chains."""
    n = len(data)
    out = bytearray()
    head = {}
    prev = [0] * n
    anchor = 0
    i = 0

    def length_bytes(v):
        while v >= 255:
            out.append(255)
            v -= 255
        out.append(v)

    def insert(pos):
        if pos + MIN_MATCH <= n:
            key = data[pos:pos + MIN_MATCH]
            prev[pos] = head.get(key, -1)
            head[key] = pos

    while i + MIN_MATCH <= n:
        key = data[i:i + MIN_MATCH]
        cand = head.get(key, -1)
        best_len, best_off = 0, 0
        tries = CHAIN
        while cand >= 0 and i - cand <= MAX_OFFSET and tries:
            length = 0
            while i + length < n and data[cand + length] == data[i + length]:
                length += 1
            if length > best_len:
                best_len, best_off = length, i - cand
            cand = prev[cand]
            tries -= 1
        if best_len < MIN_MATCH:
            insert(i)
            i += 1
            continue
        literals = i - anchor
        match = best_len - MIN_MATCH
        out.append((min(literals, 15) << 4) | min(match, 15))
        if literals >= 15:
            length_bytes(literals - 15)
        out += data[anchor:i]
        out += bytes((best_off & 255, best_off >> 8))
        if match >= 15:
            length_bytes(match - 15)
        for p in range(i, i + best_len):
            insert(p)
        i += best_len
        anchor = i

    literals = n - anchor
    out.append(min(literals, 15) << 4)
    if literals >= 15:
        length_bytes(literals - 15)
    out += data[anchor:]
    return bytes(out)


def decompress(src, size):
    """Reference decoder, to check the compressor."""
    out = bytearray()
    p = 0
    while p < len(src):
        token = src[p]
        p += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[p]
                p += 1
                lit += b
                if b != 255:
                    break
        out += src[p:p + lit]
        p += lit
        if p == len(src):
            break
        off = src[p] | src[p + 1] << 8
        p += 2
        m = token & 15
        if m == 15:
            while True:
                b = src[p]
                p += 1
                m += b
                if b != 255:
                    break
        for _ in range(m + MIN_MATCH):
            out.append(out[-off])
    assert len(out) == size
    return bytes(out)


SECTOR = 128
CCPBDOS_OFFSET = 0x880   # in cpm.bin
CCPBDOS_SIZE = 44 * SECTOR
CPMBIN_MSIZE = 64
SYSTEM_SECTORS = 2 * 26  # two tracks of an 8" floppy


def put_system(image, boot_file, bios_file, cpm_file, reloc_file, msize):
    """CP/M on the system tracks of image (a bytearray)."""
    with open(boot_file, "rb") as f:
        boot = f.read()
    with open(bios_file, "rb") as f:
        bios = f.read()
    with open(cpm_file, "rb") as f:
        ccpbdos = bytearray(f.read()[CCPBDOS_OFFSET:CCPBDOS_OFFSET + CCPBDOS_SIZE])
    with open(reloc_file, "rb") as f:
        reloc = f.read()
    if len(ccpbdos) != CCPBDOS_SIZE or len(reloc) * 8 != CCPBDOS_SIZE:
        sys.exit("cpm.bin or the relocation bitmap is the wrong size")
    pages = (msize - CPMBIN_MSIZE) * 4
    for i in range(CCPBDOS_SIZE):
        if reloc[i // 8] & (0x80 >> (i % 8)):
            ccpbdos[i] = (ccpbdos[i] + pages) & 0xff
    if len(boot) > SECTOR:
        sys.exit("boot loader over one sector")
    system = boot.ljust(SECTOR, b"\0") + ccpbdos + bios
    if len(system) > SYSTEM_SECTORS * SECTOR:
        sys.exit("CP/M system over the two system tracks")
    system = system.ljust(-(-len(system) // SECTOR) * SECTOR, b"\0")
    image[0:len(system)] = system


# The 8" floppy's CP/M file system (src/cpm/bios.asm's DPB and TRANS).
FD_SECTORS = 26
FD_SKEW = [1, 7, 13, 19, 25, 5, 11, 17, 23, 3, 9, 15, 21,
           2, 8, 14, 20, 26, 6, 12, 18, 24, 4, 10, 16, 22]
FD_RESERVED_TRACKS = 2
FD_BLOCK = 1024
FD_BLOCKS = 243          # DSM + 1
FD_DIR_ENTRIES = 64      # DRM + 1, in blocks 0 and 1
FD_DIR_BLOCKS = 2
EXTENT_RECORDS = 128     # 16 KB per directory entry (EXM 0)


def fd_sector_offset(record):
    """Image offset of logical 128-byte record (after the system tracks)."""
    track = FD_RESERVED_TRACKS + record // FD_SECTORS
    sector = FD_SKEW[record % FD_SECTORS] - 1
    return (track * FD_SECTORS + sector) * SECTOR


def fd_block(image, block, data=None):
    """Read block (or write data to it) through the skew."""
    per_block = FD_BLOCK // SECTOR
    out = b""
    for i in range(per_block):
        off = fd_sector_offset(block * per_block + i)
        if data is None:
            out += bytes(image[off:off + SECTOR])
        else:
            image[off:off + SECTOR] = data[i * SECTOR:(i + 1) * SECTOR]
    return out


def add_file(image, cpm_name, host_file):
    """host_file into the CP/M directory of image (a bytearray), user 0."""
    base, _, ext = cpm_name.upper().partition(".")
    if not (1 <= len(base) <= 8 and len(ext) <= 3):
        sys.exit(f"{cpm_name}: not an 8.3 name")
    name = base.ljust(8).encode() + ext.ljust(3).encode()
    with open(host_file, "rb") as f:
        data = f.read()
    records = -(-len(data) // SECTOR)
    data = data.ljust(records * SECTOR, b"\x1a")

    directory = bytearray(b"".join(fd_block(image, b) for b in range(FD_DIR_BLOCKS)))
    used = set(range(FD_DIR_BLOCKS))
    free_entries = []
    for i in range(FD_DIR_ENTRIES):
        entry = directory[i * 32:(i + 1) * 32]
        if entry[0] == 0xE5:
            free_entries.append(i)
            continue
        if bytes(c & 0x7F for c in entry[1:12]) == name:
            sys.exit(f"{cpm_name}: already on the disk")
        used.update(b for b in entry[16:32] if b)
    free_blocks = [b for b in range(FD_BLOCKS) if b not in used]
    blocks_needed = -(-records * SECTOR // FD_BLOCK)
    extents = max(1, -(-records // EXTENT_RECORDS))
    if blocks_needed > len(free_blocks) or extents > len(free_entries):
        sys.exit(f"{cpm_name}: no room on the disk")

    blocks = free_blocks[:blocks_needed]
    block_data = data.ljust(blocks_needed * FD_BLOCK, b"\x1a")
    for i, b in enumerate(blocks):
        fd_block(image, b, block_data[i * FD_BLOCK:(i + 1) * FD_BLOCK])
    per_extent = EXTENT_RECORDS * SECTOR // FD_BLOCK
    for x in range(extents):
        ext_blocks = blocks[x * per_extent:(x + 1) * per_extent]
        rc = min(EXTENT_RECORDS, records - x * EXTENT_RECORDS)
        entry = bytes([0]) + name + bytes([x, 0, 0, rc]) + bytes(ext_blocks).ljust(16, b"\0")
        i = free_entries[x]
        directory[i * 32:(i + 1) * 32] = entry
    for b in range(FD_DIR_BLOCKS):
        fd_block(image, b, directory[b * FD_BLOCK:(b + 1) * FD_BLOCK])


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    output = sys.argv[1]
    args = sys.argv[2:]
    system = None
    if args and args[0] == "--system":
        system = args[1:7]
        args = args[7:]
    files = []
    while args and args[0] == "--file":
        files.append(args[1:4])
        args = args[4:]
    images = []
    for arg in args:
        name, path = arg.split("=", 1)
        if not (1 <= len(name) <= 8 and name.isalnum() and name.isupper()):
            sys.exit(f"{name}: image names are 1-8 upper-case letters and digits")
        with open(path, "rb") as f:
            data = f.read()
        data = bytearray(data)
        if system and system[0] == name:
            put_system(data, *system[1:5], int(system[5]))
        for image_name, cpm_name, host_file in files:
            if image_name == name:
                add_file(data, cpm_name, host_file)
        data = bytes(data)
        packed = compress(data)
        if decompress(packed, len(data)) != data:
            sys.exit(f"{path}: compression check failed")
        images.append((name, data, packed))

    with open(output, "w") as f:
        f.write("// Generated by src/diskimg_gen.py: do not edit.\n\n")
        f.write('#include "diskimg.h"\n\n')
        f.write("#if DISKIMG_IN_FLASH\n"
                "#define DISKIMG_DATA __attribute__((section(\".flashdata.diskimg\")))\n"
                "#else\n#define DISKIMG_DATA\n#endif\n\n")
        for index, (name, data, packed) in enumerate(images):
            f.write(f"// {name}.DSK: {len(data)} bytes, {len(packed)} compressed\n")
            f.write(f"static const uint8_t data{index}[{len(packed)}] DISKIMG_DATA = {{\n")
            for start in range(0, len(packed), 16):
                row = packed[start:start + 16]
                f.write("\t" + ", ".join(f"0x{b:02x}" for b in row) + ",\n")
            f.write("};\n\n")
        f.write(f"const diskimg_t diskimg_list[{len(images)}] = {{\n")
        for index, (name, data, packed) in enumerate(images):
            crc = zlib.crc32(data) & 0xffffffff
            f.write(f'\t{{"{name}", {len(data)}u, 0x{crc:08x}u, data{index}, sizeof data{index}}},\n')
        f.write("};\n\n")
        f.write(f"const unsigned diskimg_count = {len(images)};\n")


if __name__ == "__main__":
    main()
