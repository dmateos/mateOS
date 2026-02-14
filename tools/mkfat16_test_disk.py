#!/usr/bin/env python3
"""
Build a tiny FAT16 superfloppy image.
Always includes TEST.TXT. Optionally includes a DOOM IWAD in root.
"""

import argparse
import struct


def write_le16(buf, off, v):
    buf[off : off + 2] = struct.pack("<H", v)


def write_le32(buf, off, v):
    buf[off : off + 4] = struct.pack("<I", v)


def mk_83_name(name):
    upper = name.upper()
    if "." in upper:
        base, ext = upper.rsplit(".", 1)
    else:
        base, ext = upper, ""
    base = "".join(ch for ch in base if ch.isalnum())[:8].ljust(8, " ")
    ext = "".join(ch for ch in ext if ch.isalnum())[:3].ljust(3, " ")
    return (base + ext).encode("ascii")


def round_up(v, align):
    return ((v + align - 1) // align) * align


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="fat16_test.img")
    ap.add_argument("wad", nargs="?", default=None, help="Optional path to DOOM1.WAD")
    args = ap.parse_args()

    # 8 MiB FAT16 image:
    # 512-byte sectors, 1 sec/cluster, 2 FATs, 512 root entries.
    total_sectors = 16384
    bytes_per_sector = 512
    sectors_per_cluster = 1
    reserved_sectors = 1
    fat_count = 2
    root_entries = 512
    sectors_per_fat = 64
    root_dir_sectors = (root_entries * 32 + (bytes_per_sector - 1)) // bytes_per_sector
    fat_start = reserved_sectors
    root_start = fat_start + fat_count * sectors_per_fat
    data_start = root_start + root_dir_sectors

    size = total_sectors * bytes_per_sector
    img = bytearray(size)

    # Boot sector / BPB
    bs = memoryview(img)[:512]
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"MATEFAT "
    write_le16(bs, 11, bytes_per_sector)
    bs[13] = sectors_per_cluster
    write_le16(bs, 14, reserved_sectors)
    bs[16] = fat_count
    write_le16(bs, 17, root_entries)
    write_le16(bs, 19, total_sectors)  # fits in 16-bit
    bs[21] = 0xF8
    write_le16(bs, 22, sectors_per_fat)
    write_le16(bs, 24, 63)
    write_le16(bs, 26, 16)
    write_le32(bs, 28, 0)
    write_le32(bs, 32, 0)
    bs[36] = 0x80
    bs[38] = 0x29
    write_le32(bs, 39, 0x12345678)
    bs[43:54] = b"MATEOS DISK"
    bs[54:62] = b"FAT16   "
    bs[510] = 0x55
    bs[511] = 0xAA

    files = [("TEST.TXT", b"Hello from FAT16 on ATA PIO!\n")]
    if args.wad:
        with open(args.wad, "rb") as f:
            files.append(("DOOM1.WAD", f.read()))

    # FAT tables
    for fat_i in range(fat_count):
        fat_lba = fat_start + fat_i * sectors_per_fat
        off = fat_lba * bytes_per_sector
        # cluster 0 + 1 reserved
        img[off + 0 : off + 2] = b"\xF8\xFF"
        img[off + 2 : off + 4] = b"\xFF\xFF"
    # Root directory entries
    root_off = root_start * bytes_per_sector
    data_off = data_start * bytes_per_sector
    next_cluster = 2

    for idx, (name, body) in enumerate(files):
        entry = bytearray(32)
        entry[0:11] = mk_83_name(name)
        entry[11] = 0x20  # archive

        file_size = len(body)
        clusters = max(1, round_up(file_size, bytes_per_sector) // bytes_per_sector)
        first_cluster = next_cluster

        write_le16(entry, 26, first_cluster)
        write_le32(entry, 28, file_size)
        img[root_off + idx * 32 : root_off + (idx + 1) * 32] = entry

        # Write data payload.
        file_off = data_off + (first_cluster - 2) * bytes_per_sector
        img[file_off : file_off + file_size] = body

        # Write FAT chain.
        for fat_i in range(fat_count):
            fat_lba = fat_start + fat_i * sectors_per_fat
            off = fat_lba * bytes_per_sector
            for c in range(first_cluster, first_cluster + clusters):
                fat_ent = off + c * 2
                if c == first_cluster + clusters - 1:
                    img[fat_ent : fat_ent + 2] = b"\xFF\xFF"  # EOC
                else:
                    img[fat_ent : fat_ent + 2] = struct.pack("<H", c + 1)

        next_cluster += clusters

    with open(args.out, "wb") as f:
        f.write(img)

    print(f"Wrote {args.out} ({size} bytes)")
    print("Contains root files:")
    for name, body in files:
        print(f"  {name} ({len(body)} bytes)")


if __name__ == "__main__":
    main()
