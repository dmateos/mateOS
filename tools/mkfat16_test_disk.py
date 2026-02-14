#!/usr/bin/env python3
"""
Build a tiny FAT16 superfloppy image with one root file: TEST.TXT
"""

import argparse
import struct


def write_le16(buf, off, v):
    buf[off : off + 2] = struct.pack("<H", v)


def write_le32(buf, off, v):
    buf[off : off + 4] = struct.pack("<I", v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="fat16_test.img")
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

    # FAT tables
    for fat_i in range(fat_count):
        fat_lba = fat_start + fat_i * sectors_per_fat
        off = fat_lba * bytes_per_sector
        # cluster 0 + 1 reserved, cluster 2 used by TEST.TXT and marked EOC
        img[off + 0 : off + 2] = b"\xF8\xFF"
        img[off + 2 : off + 4] = b"\xFF\xFF"
        img[off + 4 : off + 6] = b"\xFF\xFF"

    # Root directory entry for TEST.TXT
    root_off = root_start * bytes_per_sector
    entry = bytearray(32)
    entry[0:11] = b"TEST    TXT"
    entry[11] = 0x20  # archive
    write_le16(entry, 26, 2)  # first cluster
    body = b"Hello from FAT16 on ATA PIO!\n"
    write_le32(entry, 28, len(body))
    img[root_off : root_off + 32] = entry

    # File data at cluster 2
    data_off = data_start * bytes_per_sector
    img[data_off : data_off + len(body)] = body

    with open(args.out, "wb") as f:
        f.write(img)

    print(f"Wrote {args.out} ({size} bytes)")
    print("Contains root file: TEST.TXT")


if __name__ == "__main__":
    main()
