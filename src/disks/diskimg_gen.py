#!/usr/bin/env python3
"""Compress CP/M disk images into a C source for the firmware (diskimg.h).

Usage: diskimg_gen.py OUTPUT.c [--system NAME BOOT BIOS CPMBIN RELOC MSIZE]
                        [--delete NAME CPMNAME ...]
                        [--file NAME CPMNAME HOSTFILE ...]
                        NAME=IMAGE [NAME=IMAGE ...]
       diskimg_gen.py --edit IMAGE [--delete CPMNAME | --file CPMNAME HOSTFILE
                        | --extract CPMNAME HOSTFILE] ...
       diskimg_gen.py --convert IMAGE LAYOUT OUTPUT

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

The file system steps work on an 8" floppy or an 8 MB hard disk image
(the layout comes from the size).

--delete (any number) removes CPMNAME from the CP/M file system of image
NAME (user 0): its directory entries and blocks are blanked
(E5). The file must be there. Deletions come before --file additions.

--file (any number) adds HOSTFILE to the CP/M file system of image NAME
(user 0) as CPMNAME (8.3, e.g. WORMS.COM); the name must not be
there already.

--edit changes an uncompressed image in place (src/cpm/cpm3/build.sh's
work disks): --delete and --file as above, and --extract, which copies
CPMNAME off the disk into HOSTFILE (whole records), in the order given.

--convert copies every file on IMAGE (all users, attributes kept) onto a
blank image of LAYOUT (FD or HD8) written to OUTPUT.
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
        # Only the last window of the match can be matched again.
        for p in range(max(i, i + best_len - WINDOW), i + best_len):
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


class Layout:
    """A disk format's CP/M file system, as our BIOSes' DPBs give it
    (src/cpm/bios.asm, src/cpm/cpm3/bnkbios3.asm)."""

    def __init__(self, name, tracks, sectors, skew, reserved_tracks, block, blocks, dir_entries):
        self.name = name
        self.sectors = sectors
        self.skew = skew                    # 1-based physical sectors, or None
        self.reserved_tracks = reserved_tracks
        self.block = block
        self.blocks = blocks                # DSM + 1
        self.dir_entries = dir_entries      # DRM + 1, from block 0
        self.dir_blocks = dir_entries * 32 // block
        self.wide = blocks > 256            # 16-bit block numbers
        self.pointers = 8 if self.wide else 16
        self.size = tracks * sectors * SECTOR
        # Logical (16 KB) extents an entry covers: EXM + 1.
        self.extents_per_entry = self.pointers * block // (EXTENT_RECORDS * SECTOR)

    def sector_offset(self, record):
        """Image offset of logical 128-byte record (after the system tracks)."""
        track = self.reserved_tracks + record // self.sectors
        sector = record % self.sectors
        if self.skew:
            sector = self.skew[sector] - 1
        return (track * self.sectors + sector) * SECTOR

    def read_block(self, image, block):
        per_block = self.block // SECTOR
        return b"".join(bytes(image[o:o + SECTOR]) for o in
                        (self.sector_offset(block * per_block + i) for i in range(per_block)))

    def write_block(self, image, block, data):
        per_block = self.block // SECTOR
        for i in range(per_block):
            off = self.sector_offset(block * per_block + i)
            image[off:off + SECTOR] = data[i * SECTOR:(i + 1) * SECTOR]

    def read_dir(self, image):
        return bytearray(b"".join(self.read_block(image, b) for b in range(self.dir_blocks)))

    def write_dir(self, image, directory):
        for b in range(self.dir_blocks):
            self.write_block(image, b, directory[b * self.block:(b + 1) * self.block])

    def entry_blocks(self, entry):
        if self.wide:
            ptrs = [entry[16 + 2 * i] | entry[17 + 2 * i] << 8 for i in range(8)]
        else:
            ptrs = list(entry[16:32])
        return [b for b in ptrs if b]

    def pack_blocks(self, blocks):
        if self.wide:
            return b"".join(bytes((b & 255, b >> 8)) for b in blocks).ljust(16, b"\0")
        return bytes(blocks).ljust(16, b"\0")


EXTENT_RECORDS = 128     # records in a logical extent (16 KB)
LAYOUTS = [
    # 8" SSSD floppy: two system tracks, skew 6, 1 KB blocks.
    Layout("FD", 77, 26, [1, 7, 13, 19, 25, 5, 11, 17, 23, 3, 9, 15, 21,
                                 2, 8, 14, 20, 26, 6, 12, 18, 24, 4, 10, 16, 22],
           2, 1024, 243, 64),
    # 8 MB hard disk: no system tracks, no skew, 4 KB blocks.
    Layout("HD8", 256, 255, None, 0, 4096, 2040, 1024),
]


def layout_of(image):
    for layout in LAYOUTS:
        if len(image) == layout.size:
            return layout
    sys.exit(f"{len(image)} bytes: not an 8\" floppy or 8 MB disk image")


def fcb_name(cpm_name):
    """An 8.3 name in the directory's form (11 bytes, blank-padded)."""
    base, _, ext = cpm_name.upper().partition(".")
    if not (1 <= len(base) <= 8 and len(ext) <= 3):
        sys.exit(f"{cpm_name}: not an 8.3 name")
    return base.ljust(8).encode() + ext.ljust(3).encode()


def plain(name):
    """A directory name without its attribute bits."""
    return bytes(c & 0x7F for c in name)


def extent_number(entry):
    return entry[12] + 32 * entry[14]


def delete_file(image, cpm_name):
    """cpm_name out of the CP/M directory of image (a bytearray), user 0:
    its entries and blocks blanked, as on a fresh disk."""
    layout = layout_of(image)
    name = fcb_name(cpm_name)
    directory = layout.read_dir(image)
    blocks = []
    found = False
    for i in range(layout.dir_entries):
        entry = directory[i * 32:(i + 1) * 32]
        if entry[0] == 0 and plain(entry[1:12]) == name:
            found = True
            blocks += layout.entry_blocks(entry)
            directory[i * 32:(i + 1) * 32] = b"\xe5" * 32
    if not found:
        sys.exit(f"{cpm_name}: not on the disk")
    for b in blocks:
        layout.write_block(image, b, b"\xe5" * layout.block)
    layout.write_dir(image, directory)


def put_file(image, user, name, data, last_bytes=0):
    """data (whole records) into the CP/M directory of image as name (11
    bytes, attribute bits kept) for user; last_bytes is the directory's S1
    byte of the last entry (CP/M 3's byte count of the last record)."""
    layout = layout_of(image)
    records = len(data) // SECTOR
    directory = layout.read_dir(image)
    used = set(range(layout.dir_blocks))
    free_entries = []
    for i in range(layout.dir_entries):
        entry = directory[i * 32:(i + 1) * 32]
        if entry[0] == 0xE5:
            free_entries.append(i)
            continue
        if entry[0] == user and plain(entry[1:12]) == plain(name):
            sys.exit(f"{plain(name).decode()}: already on the disk")
        if entry[0] < 32:
            used.update(layout.entry_blocks(entry))
    free_blocks = [b for b in range(layout.blocks) if b not in used]
    blocks_needed = -(-records * SECTOR // layout.block)
    entry_records = EXTENT_RECORDS * layout.extents_per_entry
    entries = max(1, -(-records // entry_records))
    if blocks_needed > len(free_blocks) or entries > len(free_entries):
        sys.exit(f"{plain(name).decode()}: no room on the disk")

    blocks = free_blocks[:blocks_needed]
    block_data = data.ljust(blocks_needed * layout.block, b"\x1a")
    for i, b in enumerate(blocks):
        layout.write_block(image, b, block_data[i * layout.block:(i + 1) * layout.block])
    for x in range(entries):
        in_entry = min(entry_records, records - x * entry_records)
        # The entry's extent number is its last logical extent's; RC
        # counts that extent's records.
        last = max(0, in_entry - 1) // EXTENT_RECORDS
        extent = x * layout.extents_per_entry + last
        rc = in_entry - last * EXTENT_RECORDS
        s1 = last_bytes if x == entries - 1 else 0
        entry = (bytes([user]) + name + bytes([extent & 31, s1, extent >> 5, rc])
                 + layout.pack_blocks(blocks[x * layout.pointers:(x + 1) * layout.pointers]))
        i = free_entries[x]
        directory[i * 32:(i + 1) * 32] = entry
    layout.write_dir(image, directory)


def add_file(image, cpm_name, host_file):
    """host_file into the CP/M directory of image (a bytearray), user 0."""
    with open(host_file, "rb") as f:
        data = f.read()
    records = -(-len(data) // SECTOR)
    put_file(image, 0, fcb_name(cpm_name), data.ljust(records * SECTOR, b"\x1a"))


def files(image):
    """Every file on image: (user, name with attributes, data, S1 of its
    last entry), whole records."""
    layout = layout_of(image)
    directory = layout.read_dir(image)
    found = {}
    for i in range(layout.dir_entries):
        entry = bytes(directory[i * 32:(i + 1) * 32])
        if entry[0] < 16:
            found.setdefault((entry[0], plain(entry[1:12])), []).append(entry)
    out = []
    for (user, name), entries in found.items():
        entries.sort(key=extent_number)
        data = b""
        for n, entry in enumerate(entries):
            in_entry = (extent_number(entry) % layout.extents_per_entry) * EXTENT_RECORDS + entry[15]
            if n != extent_number(entry) // layout.extents_per_entry:
                sys.exit(f"{name.decode()}: an extent is missing")
            blocks = b"".join(layout.read_block(image, b) for b in layout.entry_blocks(entry))
            data += blocks[:in_entry * SECTOR]
        out.append((user, entries[-1][1:12], data, entries[-1][13]))
    return out


def extract_file(image, cpm_name, host_file):
    """cpm_name (user 0) from the CP/M directory of image into host_file,
    whole records (a text file keeps its ^Z padding)."""
    name = fcb_name(cpm_name)
    for user, fname, data, _ in files(image):
        if user == 0 and plain(fname) == name:
            with open(host_file, "wb") as f:
                f.write(data)
            return
    sys.exit(f"{cpm_name}: not on the disk")


def convert(source, layout_name, output):
    """--convert: every file on image source onto a blank image of layout
    layout_name (users, attributes and byte counts kept)."""
    with open(source, "rb") as f:
        src = f.read()
    layout = next((l for l in LAYOUTS if l.name == layout_name), None)
    if not layout:
        sys.exit(f"{layout_name}: no such layout")
    image = bytearray(b"\xe5" * layout.size)
    for user, name, data, last_bytes in files(src):
        put_file(image, user, name, data, last_bytes)
    with open(output, "wb") as f:
        f.write(image)


def edit(args):
    """--edit IMAGE: the --delete, --file and --extract steps that follow
    (in that order, each as given) on the uncompressed IMAGE, in place."""
    path = args[0]
    args = args[1:]
    with open(path, "rb") as f:
        image = bytearray(f.read())
    steps = {"--delete": 1, "--file": 2, "--extract": 2}
    while args:
        op = args[0]
        if op not in steps or len(args) <= steps[op]:
            sys.exit(__doc__)
        operands = args[1:1 + steps[op]]
        args = args[1 + steps[op]:]
        if op == "--delete":
            delete_file(image, *operands)
        elif op == "--file":
            add_file(image, *operands)
        else:
            extract_file(image, *operands)
    with open(path, "wb") as f:
        f.write(image)


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--convert":
        convert(*sys.argv[2:])
        return
    if len(sys.argv) >= 3 and sys.argv[1] == "--edit":
        edit(sys.argv[2:])
        return
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    output = sys.argv[1]
    args = sys.argv[2:]
    system = None
    if args and args[0] == "--system":
        system = args[1:7]
        args = args[7:]
    deletions = []
    while args and args[0] == "--delete":
        deletions.append(args[1:3])
        args = args[3:]
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
        for image_name, cpm_name in deletions:
            if image_name == name:
                delete_file(data, cpm_name)
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
