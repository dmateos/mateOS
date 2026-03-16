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


def needs_lfn(name):
    """Return True if name cannot be represented exactly as 8.3."""
    upper = name.upper()
    if "." in upper:
        base, ext = upper.rsplit(".", 1)
    else:
        base, ext = upper, ""
    valid = lambda ch: ch.isalnum() or ch in ('_', '-', '~', '!', '#', '$', '%', '&', '@')
    clean_base = "".join(ch for ch in base if valid(ch))
    clean_ext = "".join(ch for ch in ext if valid(ch))
    # Needs LFN if base > 8 chars, ext > 3 chars, or any chars were stripped
    return (len(clean_base) > 8 or len(clean_ext) > 3 or
            clean_base != base or clean_ext != ext)


def mk_83_alias(name, n=1):
    """Generate a unique ~N short alias for a long filename."""
    upper = name.upper()
    if "." in upper:
        base, ext = upper.rsplit(".", 1)
    else:
        base, ext = upper, ""
    valid = lambda ch: ch.isalnum() or ch in ('_', '-', '~', '!', '#', '$', '%', '&', '@')
    clean_base = "".join(ch for ch in base if valid(ch))
    clean_ext = "".join(ch for ch in ext if valid(ch))[:3]
    suffix = "~%d" % n
    # Truncate base to leave room for ~N suffix (max 8 total)
    trunc_base = clean_base[:8 - len(suffix)]
    short_base = (trunc_base + suffix).ljust(8, " ")
    short_ext = clean_ext.ljust(3, " ")
    return (short_base + short_ext).encode("ascii")


def lfn_checksum(name83):
    """Compute the VFAT LFN checksum over the 11-byte 8.3 name."""
    assert len(name83) == 11
    csum = 0
    for b in name83:
        csum = (((csum & 1) << 7) | ((csum & 0xFE) >> 1)) + b
        csum &= 0xFF
    return csum


def lfn_entries_for(name):
    """
    Build the list of 32-byte LFN directory entries for `name`.
    Returns them in the order they must be written to disk (last-seq first,
    i.e. highest sequence number first with LAST_LFN flag set, then
    descending to seq 1), followed by the 8.3 short entry.
    Caller writes these entries in the returned order.
    """
    # Encode name as UTF-16LE, null-terminated, padded with 0xFFFF
    encoded = name.encode("utf-16-le")
    # Each LFN entry holds 13 UTF-16LE characters = 26 bytes
    chars_per_entry = 13
    # Pad to multiple of chars_per_entry with null then 0xFFFF fill
    char_count = len(encoded) // 2 + 1  # +1 for null terminator
    padded_chars = char_count
    remainder = padded_chars % chars_per_entry
    if remainder:
        padded_chars += chars_per_entry - remainder
    # Build flat array of UTF-16LE words
    utf16_words = list(struct.unpack_from("<%dH" % (len(encoded) // 2), encoded))
    utf16_words.append(0x0000)  # null terminator
    while len(utf16_words) < padded_chars:
        utf16_words.append(0xFFFF)

    num_entries = padded_chars // chars_per_entry
    return num_entries, utf16_words


def build_lfn_dirents(name, name83):
    """
    Return list of 32-byte bytearrays: LFN entries (high-seq first) in the
    order they should be written to the directory before the 8.3 entry.
    """
    csum = lfn_checksum(name83)
    num_entries, utf16_words = lfn_entries_for(name)

    entries = []
    for seq in range(num_entries, 0, -1):
        e = bytearray(32)
        seq_byte = seq
        if seq == num_entries:
            seq_byte |= 0x40  # LAST_LFN flag
        e[0] = seq_byte
        e[11] = 0x0F   # LFN attribute
        e[12] = 0x00   # type
        e[13] = csum
        e[26] = 0x00   # cluster lo (always 0 for LFN)
        e[27] = 0x00
        # Fill in the 13 UTF-16LE characters for this entry
        # Chars for seq s occupy positions [(s-1)*13 .. s*13-1]
        idx_base = (seq - 1) * 13
        chars = utf16_words[idx_base : idx_base + 13]
        # name1: chars 0-4 at bytes 1-10
        for i in range(5):
            struct.pack_into("<H", e, 1 + i * 2, chars[i])
        # name2: chars 5-10 at bytes 14-23
        for i in range(6):
            struct.pack_into("<H", e, 14 + i * 2, chars[5 + i])
        # name3: chars 11-12 at bytes 28-31
        for i in range(2):
            struct.pack_into("<H", e, 28 + i * 2, chars[11 + i])
        entries.append(bytes(e))
    return entries


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

    def _write_dirent_with_lfn(self, dir_offset, entry_idx, name83, lfn_entries, attr, first_cluster, file_size):
        """Write optional LFN entries followed by a 32-byte short directory entry.
        Returns the number of directory slots consumed (1 + len(lfn_entries))."""
        idx = entry_idx
        for lfn_e in lfn_entries:
            off = dir_offset + idx * 32
            self.img[off : off + 32] = lfn_e
            idx += 1
        # Write the short 8.3 entry
        off = dir_offset + idx * 32
        entry = bytearray(32)
        entry[0:11] = name83
        entry[11] = attr
        write_le16(entry, 26, first_cluster)
        write_le32(entry, 28, file_size)
        self.img[off : off + 32] = entry
        return idx - entry_idx + 1  # total slots used

    def _make_name83_and_lfn(self, name):
        """Return (name83_bytes, lfn_entries_list).
        lfn_entries_list is non-empty only when name requires LFN."""
        if needs_lfn(name):
            name83 = mk_83_alias(name)
            lfn_entries = build_lfn_dirents(name, name83)
        else:
            name83 = mk_83_name(name)
            lfn_entries = []
        return name83, lfn_entries

    def add_root_file(self, name, data):
        """Add a file to the root directory."""
        file_size = len(data)
        clusters = max(1, round_up(file_size, self.bytes_per_sector) // self.bytes_per_sector)
        first_cluster = self._alloc_clusters(clusters)

        name83, lfn_entries = self._make_name83_and_lfn(name)
        root_off = self.root_start * self.bytes_per_sector
        slots = self._write_dirent_with_lfn(root_off, self.root_entry_idx, name83, lfn_entries, 0x20, first_cluster, file_size)
        self.root_entry_idx += slots

        file_off = self._cluster_offset(first_cluster)
        self.img[file_off : file_off + file_size] = data
        return first_cluster

    def create_subdir(self, name):
        """Create a subdirectory in the root directory. Returns the cluster number."""
        # Allocate one cluster for the directory
        dir_cluster = self._alloc_clusters(1)

        name83, lfn_entries = self._make_name83_and_lfn(name)
        # Add entry to root directory
        root_off = self.root_start * self.bytes_per_sector
        slots = self._write_dirent_with_lfn(root_off, self.root_entry_idx, name83, lfn_entries, 0x10, dir_cluster, 0)
        self.root_entry_idx += slots

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

    def _extend_dir_cluster(self, dir_cluster):
        """Allocate and link a new cluster to the end of a directory chain. Returns new cluster offset."""
        new_cluster = self._alloc_clusters(1)
        for fat_i in range(self.fat_count):
            fat_lba = self.fat_start + fat_i * self.sectors_per_fat
            fat_off = fat_lba * self.bytes_per_sector
            c = dir_cluster
            while True:
                next_c = read_le16(self.img, fat_off + c * 2)
                if next_c >= 0xFFF8:
                    self.img[fat_off + c * 2 : fat_off + c * 2 + 2] = struct.pack("<H", new_cluster)
                    break
                c = next_c
        new_off = self._cluster_offset(new_cluster)
        cluster_bytes = self.bytes_per_sector * self.sectors_per_cluster
        self.img[new_off : new_off + cluster_bytes] = b"\x00" * cluster_bytes
        return new_off

    def add_file_to_dir(self, dir_cluster, name, data):
        """Add a file to a subdirectory, writing LFN entries when the name needs it."""
        file_size = len(data)
        clusters = max(1, round_up(file_size, self.bytes_per_sector) // self.bytes_per_sector)
        first_cluster = self._alloc_clusters(clusters)

        name83, lfn_entries = self._make_name83_and_lfn(name)
        slots_needed = 1 + len(lfn_entries)

        entries_per_cluster = (self.bytes_per_sector * self.sectors_per_cluster) // 32

        # Walk the entire cluster chain looking for enough contiguous free slots.
        # (LFN entries + short entry must all be within the same cluster run —
        # in practice they fit easily since a cluster is 512 bytes = 16 entries.)
        dir_off = None
        entry_idx = None
        c = dir_cluster
        while c >= 2 and c < 0xFFF8:
            c_off = self._cluster_offset(c)
            # Scan for a run of `slots_needed` consecutive free entries
            run_start = None
            run_len = 0
            for idx in range(entries_per_cluster):
                e_off = c_off + idx * 32
                if self.img[e_off] == 0x00 or self.img[e_off] == 0xE5:
                    if run_start is None:
                        run_start = idx
                        run_len = 1
                    else:
                        run_len += 1
                    if run_len >= slots_needed:
                        dir_off = c_off
                        entry_idx = run_start
                        break
                else:
                    run_start = None
                    run_len = 0
            if dir_off is not None:
                break
            # Follow FAT chain to next cluster
            fat_off = self.fat_start * self.bytes_per_sector + c * 2
            c = read_le16(self.img, fat_off)

        if dir_off is None:
            # All clusters full — allocate another cluster and link it
            dir_off = self._extend_dir_cluster(dir_cluster)
            entry_idx = 0

        self._write_dirent_with_lfn(dir_off, entry_idx, name83, lfn_entries, 0x20, first_cluster, file_size)

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
