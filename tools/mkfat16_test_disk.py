#!/usr/bin/env python3
"""
Build a FAT16 superfloppy image for mateOS boot disk.
Supports subdirectories via --add-dir and root files via --add.
Always includes TEST.TXT. Optionally includes a DOOM IWAD in root.
"""

import argparse
import glob as globmod
import os
import struct


def write_le16(buf, off, v):
    buf[off : off + 2] = struct.pack("<H", v)


def write_le32(buf, off, v):
    buf[off : off + 4] = struct.pack("<I", v)


def read_le16(buf, off):
    return struct.unpack("<H", buf[off : off + 2])[0]


def mk_83_name(name):
    upper = name.upper()
    if "." in upper:
        base, ext = upper.rsplit(".", 1)
    else:
        base, ext = upper, ""
    # FAT16 8.3 allows A-Z, 0-9, and several special chars including _ and -
    valid = lambda ch: ch.isalnum() or ch in ('_', '-', '~', '!', '#', '$', '%', '&', '@')
    base = "".join(ch for ch in base if valid(ch))[:8].ljust(8, " ")
    ext = "".join(ch for ch in ext if valid(ch))[:3].ljust(3, " ")
    return (base + ext).encode("ascii")


def round_up(v, align):
    return ((v + align - 1) // align) * align


class Fat16Builder:
    """Builds a FAT16 superfloppy image with subdirectory support."""

    def __init__(self, total_sectors=32768):
        self.bytes_per_sector = 512
        self.sectors_per_cluster = 1
        self.reserved_sectors = 1
        self.fat_count = 2
        self.root_entries = 512
        self.sectors_per_fat = 128
        self.total_sectors = total_sectors

        self.root_dir_sectors = (
            self.root_entries * 32 + (self.bytes_per_sector - 1)
        ) // self.bytes_per_sector
        self.fat_start = self.reserved_sectors
        self.root_start = self.fat_start + self.fat_count * self.sectors_per_fat
        self.data_start = self.root_start + self.root_dir_sectors

        size = self.total_sectors * self.bytes_per_sector
        self.img = bytearray(size)
        self.next_cluster = 2
        self.root_entry_idx = 0

        self._write_boot_sector()
        self._init_fat()

    def _write_boot_sector(self):
        bs = memoryview(self.img)[:512]
        bs[0:3] = b"\xEB\x3C\x90"
        bs[3:11] = b"MATEFAT "
        write_le16(bs, 11, self.bytes_per_sector)
        bs[13] = self.sectors_per_cluster
        write_le16(bs, 14, self.reserved_sectors)
        bs[16] = self.fat_count
        write_le16(bs, 17, self.root_entries)
        if self.total_sectors <= 0xFFFF:
            write_le16(bs, 19, self.total_sectors)
        else:
            write_le16(bs, 19, 0)
            write_le32(bs, 32, self.total_sectors)
        bs[21] = 0xF8
        write_le16(bs, 22, self.sectors_per_fat)
        write_le16(bs, 24, 63)
        write_le16(bs, 26, 16)
        write_le32(bs, 28, 0)
        bs[36] = 0x80
        bs[38] = 0x29
        write_le32(bs, 39, 0x12345678)
        bs[43:54] = b"MATEOS DISK"
        bs[54:62] = b"FAT16   "
        bs[510] = 0x55
        bs[511] = 0xAA

    def _init_fat(self):
        for fat_i in range(self.fat_count):
            fat_lba = self.fat_start + fat_i * self.sectors_per_fat
            off = fat_lba * self.bytes_per_sector
            self.img[off + 0 : off + 2] = b"\xF8\xFF"
            self.img[off + 2 : off + 4] = b"\xFF\xFF"

    def _cluster_offset(self, cluster):
        """Byte offset in image for given data cluster."""
        return self.data_start * self.bytes_per_sector + (cluster - 2) * self.bytes_per_sector

    def _alloc_clusters(self, count):
        """Allocate a contiguous chain of clusters, return first cluster."""
        first = self.next_cluster
        for fat_i in range(self.fat_count):
            fat_lba = self.fat_start + fat_i * self.sectors_per_fat
            off = fat_lba * self.bytes_per_sector
            for c in range(first, first + count):
                fat_ent = off + c * 2
                if c == first + count - 1:
                    self.img[fat_ent : fat_ent + 2] = b"\xFF\xFF"  # EOC
                else:
                    self.img[fat_ent : fat_ent + 2] = struct.pack("<H", c + 1)
        self.next_cluster += count
        return first

    def _write_dirent(self, dir_offset, entry_idx, name83, attr, first_cluster, file_size):
        """Write a 32-byte directory entry."""
        off = dir_offset + entry_idx * 32
        entry = bytearray(32)
        entry[0:11] = name83
        entry[11] = attr
        write_le16(entry, 26, first_cluster)
        write_le32(entry, 28, file_size)
        self.img[off : off + 32] = entry

    def add_root_file(self, name, data):
        """Add a file to the root directory."""
        file_size = len(data)
        clusters = max(1, round_up(file_size, self.bytes_per_sector) // self.bytes_per_sector)
        first_cluster = self._alloc_clusters(clusters)

        root_off = self.root_start * self.bytes_per_sector
        self._write_dirent(root_off, self.root_entry_idx, mk_83_name(name), 0x20, first_cluster, file_size)
        self.root_entry_idx += 1

        file_off = self._cluster_offset(first_cluster)
        self.img[file_off : file_off + file_size] = data
        return first_cluster

    def create_subdir(self, name):
        """Create a subdirectory in the root directory. Returns the cluster number."""
        # Allocate one cluster for the directory
        dir_cluster = self._alloc_clusters(1)

        # Add entry to root directory
        root_off = self.root_start * self.bytes_per_sector
        self._write_dirent(root_off, self.root_entry_idx, mk_83_name(name), 0x10, dir_cluster, 0)
        self.root_entry_idx += 1

        # Initialize the directory cluster with . and .. entries
        dir_off = self._cluster_offset(dir_cluster)
        # Clear the cluster
        self.img[dir_off : dir_off + self.bytes_per_sector] = b"\x00" * self.bytes_per_sector

        # "." entry points to self
        dot = bytearray(32)
        dot[0:11] = b".          "
        dot[11] = 0x10
        write_le16(dot, 26, dir_cluster)
        self.img[dir_off : dir_off + 32] = dot

        # ".." entry points to root (cluster 0 for root)
        dotdot = bytearray(32)
        dotdot[0:11] = b"..         "
        dotdot[11] = 0x10
        write_le16(dotdot, 26, 0)
        self.img[dir_off + 32 : dir_off + 64] = dotdot

        return dir_cluster

    def add_file_to_dir(self, dir_cluster, name, data):
        """Add a file to a subdirectory."""
        file_size = len(data)
        clusters = max(1, round_up(file_size, self.bytes_per_sector) // self.bytes_per_sector)
        first_cluster = self._alloc_clusters(clusters)

        entries_per_cluster = (self.bytes_per_sector * self.sectors_per_cluster) // 32

        # Walk the entire cluster chain looking for a free directory entry
        dir_off = None
        entry_idx = None
        c = dir_cluster
        while c >= 2 and c < 0xFFF8:
            c_off = self._cluster_offset(c)
            for idx in range(entries_per_cluster):
                e_off = c_off + idx * 32
                if self.img[e_off] == 0x00 or self.img[e_off] == 0xE5:
                    dir_off = c_off
                    entry_idx = idx
                    break
            if dir_off is not None:
                break
            # Follow FAT chain to next cluster
            fat_off = self.fat_start * self.bytes_per_sector + c * 2
            c = read_le16(self.img, fat_off)

        if dir_off is None:
            # All clusters full — allocate another cluster and link it
            new_cluster = self._alloc_clusters(1)
            for fat_i in range(self.fat_count):
                fat_lba = self.fat_start + fat_i * self.sectors_per_fat
                fat_off = fat_lba * self.bytes_per_sector
                # Walk chain to find last cluster
                c = dir_cluster
                while True:
                    next_c = read_le16(self.img, fat_off + c * 2)
                    if next_c >= 0xFFF8:
                        self.img[fat_off + c * 2 : fat_off + c * 2 + 2] = struct.pack("<H", new_cluster)
                        break
                    c = next_c
            # Clear new cluster
            new_off = self._cluster_offset(new_cluster)
            cluster_bytes = self.bytes_per_sector * self.sectors_per_cluster
            self.img[new_off : new_off + cluster_bytes] = b"\x00" * cluster_bytes
            dir_off = new_off
            entry_idx = 0

        self._write_dirent(dir_off, entry_idx, mk_83_name(name), 0x20, first_cluster, file_size)

        file_off = self._cluster_offset(first_cluster)
        self.img[file_off : file_off + file_size] = data
        return first_cluster

    def write(self, path):
        with open(path, "wb") as f:
            f.write(self.img)


def expand_paths(paths):
    """Expand glob patterns in path list."""
    result = []
    for p in paths:
        expanded = sorted(globmod.glob(p))
        if expanded:
            result.extend(expanded)
        else:
            result.append(p)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="boot.img")
    ap.add_argument("wad", nargs="?", default=None, help="Optional path to DOOM1.WAD")
    ap.add_argument(
        "--add",
        action="append",
        default=[],
        help="Additional host file to include in FAT16 root (can repeat)",
    )
    ap.add_argument(
        "--add-dir",
        action="append",
        nargs="+",
        metavar=("DIRNAME", "FILE"),
        help="Create subdirectory and add files to it: --add-dir bin file1 file2 ...",
    )
    ap.add_argument(
        "--size-mb",
        type=int,
        default=0,
        help="Image size in MiB (0 = auto-size based on payload)",
    )
    args = ap.parse_args()

    # Collect all files to estimate size
    root_files = []
    if args.wad:
        with open(args.wad, "rb") as f:
            root_files.append(("DOOM1.WAD", f.read()))
    for p in args.add:
        with open(p, "rb") as f:
            root_files.append((os.path.basename(p), f.read()))

    dir_files = {}  # dirname -> [(filename, data), ...]
    if args.add_dir:
        for entry in args.add_dir:
            dirname = entry[0]
            file_patterns = entry[1:]
            expanded = expand_paths(file_patterns)
            if dirname not in dir_files:
                dir_files[dirname] = []
            for p in expanded:
                if os.path.isfile(p):
                    with open(p, "rb") as f:
                        dir_files[dirname].append((os.path.basename(p), f.read()))

    # Calculate total payload size
    total_payload = sum(len(d) for _, d in root_files)
    for dirname, files in dir_files.items():
        total_payload += sum(len(d) for _, d in files)

    # Auto-size: payload + overhead + 25% headroom, minimum 16 MiB
    if args.size_mb > 0:
        size_mb = args.size_mb
    else:
        size_mb = max(16, (total_payload * 5 // 4 + 1024 * 1024) // (1024 * 1024) + 1)
        # Cap at 32 MiB for reasonable image size
        if size_mb > 32:
            size_mb = 32

    total_sectors = size_mb * 2048  # 2048 sectors per MiB

    builder = Fat16Builder(total_sectors)

    # Add root files
    for name, data in root_files:
        builder.add_root_file(name, data)

    # Create subdirectories and add files
    for dirname, files in dir_files.items():
        dir_cluster = builder.create_subdir(dirname)
        for name, data in files:
            builder.add_file_to_dir(dir_cluster, name, data)

    builder.write(args.out)

    size = total_sectors * 512
    print(f"Wrote {args.out} ({size} bytes, {size_mb} MiB)")
    print(f"Root files:")
    for name, body in root_files:
        print(f"  {name} ({len(body)} bytes)")
    for dirname, files in dir_files.items():
        print(f"Directory /{dirname}/:")
        for name, body in files:
            print(f"  {name} ({len(body)} bytes)")
        print(f"  ({len(files)} files)")


if __name__ == "__main__":
    main()
