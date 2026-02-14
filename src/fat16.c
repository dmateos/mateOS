#include "fat16.h"
#include "drivers/ata_pio.h"
#include "lib.h"

#define FAT16_SECTOR_SIZE 512
#define FAT16_MAX_OPEN    16

#define FAT16_ATTR_READONLY 0x01
#define FAT16_ATTR_HIDDEN   0x02
#define FAT16_ATTR_SYSTEM   0x04
#define FAT16_ATTR_VOLUMEID 0x08
#define FAT16_ATTR_DIR      0x10
#define FAT16_ATTR_ARCHIVE  0x20
#define FAT16_ATTR_LFN      0x0F

#define FAT16_EOC 0xFFFF

typedef struct __attribute__((packed)) {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} fat16_bpb_t;

typedef struct __attribute__((packed)) {
    uint8_t name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} fat16_dirent_t;

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
} mbr_part_t;

typedef struct {
    int mounted;
    uint32_t part_lba;
    uint32_t fat_start_lba;
    uint32_t root_start_lba;
    uint32_t data_start_lba;
    uint32_t root_dir_sectors;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint8_t fat_count;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t root_entry_count;
    uint32_t cluster_count;
} fat16_state_t;

typedef struct {
    int in_use;
    int flags;
    uint16_t first_cluster;
    uint32_t size;
    uint32_t pos;
    uint8_t attr;
    uint32_t dirent_lba;
    uint16_t dirent_off;
} fat16_open_t;

static fat16_state_t g_fat;
static fat16_open_t g_open[FAT16_MAX_OPEN];

static int is_fat16_part_type(uint8_t type) {
    return (type == 0x04 || type == 0x06 || type == 0x0E);
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static int is_83_char(char c) {
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_' || c == '$' || c == '~' || c == '-' || c == '!') return 1;
    return 0;
}

static int ata_read_sector(uint32_t lba, uint8_t *out) {
    return ata_pio_read(lba, 1, out);
}

static int ata_write_sector(uint32_t lba, const uint8_t *in) {
    return ata_pio_write(lba, 1, in);
}

static uint32_t cluster_to_lba(uint16_t cluster) {
    return g_fat.data_start_lba +
           ((uint32_t)(cluster - 2) * g_fat.sectors_per_cluster);
}

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t fat16_get_entry(uint16_t cluster) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_sec = g_fat.fat_start_lba + (fat_offset / FAT16_SECTOR_SIZE);
    uint32_t ent_off = fat_offset % FAT16_SECTOR_SIZE;

    if (ata_read_sector(fat_sec, sec) < 0) return FAT16_EOC;
    return read_u16(sec + ent_off);
}

static int fat16_set_entry(uint16_t cluster, uint16_t value) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_rel_sec = fat_offset / FAT16_SECTOR_SIZE;
    uint32_t ent_off = fat_offset % FAT16_SECTOR_SIZE;

    // Mirror to all FAT copies
    for (uint8_t fat_i = 0; fat_i < g_fat.fat_count; fat_i++) {
        uint32_t fat_sec = g_fat.fat_start_lba + fat_i * g_fat.sectors_per_fat + fat_rel_sec;
        if (ata_read_sector(fat_sec, sec) < 0) return -1;
        write_u16(sec + ent_off, value);
        if (ata_write_sector(fat_sec, sec) < 0) return -1;
    }

    return 0;
}

static int fat16_alloc_cluster(uint16_t *out_cluster) {
    if (!out_cluster) return -1;

    for (uint32_t c = 2; c < g_fat.cluster_count + 2; c++) {
        uint16_t v = fat16_get_entry((uint16_t)c);
        if (v == 0x0000) {
            if (fat16_set_entry((uint16_t)c, FAT16_EOC) < 0) return -1;

            // Zero the newly allocated cluster on disk
            uint8_t zero[FAT16_SECTOR_SIZE];
            memset(zero, 0, sizeof(zero));
            uint32_t lba = cluster_to_lba((uint16_t)c);
            for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
                if (ata_write_sector(lba + s, zero) < 0) return -1;
            }

            *out_cluster = (uint16_t)c;
            return 0;
        }
    }
    return -1;
}

static int fat16_free_chain(uint16_t first) {
    uint16_t c = first;
    while (c >= 2 && c < 0xFFF8) {
        uint16_t next = fat16_get_entry(c);
        if (fat16_set_entry(c, 0x0000) < 0) return -1;
        if (next == c) break;
        c = next;
    }
    return 0;
}

static int fat16_ensure_cluster_for_index(fat16_open_t *f, uint32_t idx, uint16_t *out_cluster) {
    if (!f || !out_cluster) return -1;

    if (f->first_cluster < 2) {
        uint16_t n;
        if (fat16_alloc_cluster(&n) < 0) return -1;
        f->first_cluster = n;
    }

    uint16_t c = f->first_cluster;
    for (uint32_t i = 0; i < idx; i++) {
        uint16_t next = fat16_get_entry(c);
        if (next >= 0xFFF8 || next < 2) {
            uint16_t n;
            if (fat16_alloc_cluster(&n) < 0) return -1;
            if (fat16_set_entry(c, n) < 0) return -1;
            if (fat16_set_entry(n, FAT16_EOC) < 0) return -1;
            next = n;
        }
        c = next;
    }

    *out_cluster = c;
    return 0;
}

static void fat16_dirent_name_to_string(const fat16_dirent_t *de, char *out) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (de->name[i] == ' ') break;
        out[p++] = (char)de->name[i];
    }

    int has_ext = 0;
    for (int i = 8; i < 11; i++) {
        if (de->name[i] != ' ') {
            has_ext = 1;
            break;
        }
    }

    if (has_ext) {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (de->name[i] == ' ') break;
            out[p++] = (char)de->name[i];
        }
    }
    out[p] = '\0';
}

static const char *fat16_basename(const char *path) {
    if (!path) return path;
    while (*path == '/') path++;
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

static int fat16_name_to_83(const char *in, uint8_t out[11]) {
    if (!in || !*in) return -1;

    for (int i = 0; i < 11; i++) out[i] = ' ';

    int i = 0;
    int base_len = 0;
    while (in[i] && in[i] != '.') {
        char c = upper_ascii(in[i]);
        if (!is_83_char(c)) return -1;
        if (base_len >= 8) return -1;
        out[base_len++] = (uint8_t)c;
        i++;
    }
    if (base_len == 0) return -1;

    if (in[i] == '.') {
        i++;
        int ext_len = 0;
        while (in[i]) {
            char c = upper_ascii(in[i]);
            if (!is_83_char(c)) return -1;
            if (ext_len >= 3) return -1;
            out[8 + ext_len] = (uint8_t)c;
            ext_len++;
            i++;
        }
    }

    return 0;
}

static int fat16_lookup_root_raw(const uint8_t name83[11], fat16_dirent_t *out,
                                 uint32_t *out_lba, uint16_t *out_off,
                                 uint32_t *free_lba, uint16_t *free_off) {
    if (!g_fat.mounted || !name83) return -1;

    uint8_t sec[FAT16_SECTOR_SIZE];
    for (uint32_t s = 0; s < g_fat.root_dir_sectors; s++) {
        uint32_t lba = g_fat.root_start_lba + s;
        if (ata_read_sector(lba, sec) < 0) return -1;

        for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
            fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);

            if (de->name[0] == 0x00) {
                if (free_lba && *free_lba == 0) {
                    *free_lba = lba;
                    *free_off = (uint16_t)off;
                }
                return -1;
            }

            if (de->name[0] == 0xE5) {
                if (free_lba && *free_lba == 0) {
                    *free_lba = lba;
                    *free_off = (uint16_t)off;
                }
                continue;
            }
            if (de->attr == FAT16_ATTR_LFN) continue;
            if (de->attr & FAT16_ATTR_VOLUMEID) continue;

            if (memcmp(de->name, name83, 11) == 0) {
                if (out) *out = *de;
                if (out_lba) *out_lba = lba;
                if (out_off) *out_off = (uint16_t)off;
                return 0;
            }
        }
    }
    return -1;
}

static int fat16_lookup_root(const char *path, fat16_dirent_t *out,
                             uint32_t *out_lba, uint16_t *out_off,
                             uint8_t name83[11],
                             uint32_t *free_lba, uint16_t *free_off) {
    if (!g_fat.mounted || !path) return -1;

    const char *name = fat16_basename(path);
    if (!name || !*name) return -1;

    uint8_t local83[11];
    if (fat16_name_to_83(name, local83) < 0) return -1;
    if (name83) memcpy(name83, local83, 11);

    if (free_lba) *free_lba = 0;
    if (free_off) *free_off = 0;

    return fat16_lookup_root_raw(local83, out, out_lba, out_off, free_lba, free_off);
}

static int fat16_update_dirent(fat16_open_t *f) {
    if (!f || f->dirent_lba == 0) return -1;
    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(f->dirent_lba, sec) < 0) return -1;

    fat16_dirent_t *de = (fat16_dirent_t *)(sec + f->dirent_off);
    de->first_cluster_lo = f->first_cluster;
    de->file_size = f->size;
    de->attr = f->attr;

    if (ata_write_sector(f->dirent_lba, sec) < 0) return -1;
    return 0;
}

static int fat16_read_file(uint16_t first_cluster, uint32_t pos, void *buf, uint32_t len) {
    if (len == 0) return 0;

    if (first_cluster < 2) return 0;

    uint8_t *out = (uint8_t *)buf;
    uint32_t cluster_size = (uint32_t)g_fat.sectors_per_cluster * FAT16_SECTOR_SIZE;
    uint32_t skip_clusters = pos / cluster_size;
    uint32_t in_cluster = pos % cluster_size;

    uint16_t cl = first_cluster;
    for (uint32_t i = 0; i < skip_clusters; i++) {
        uint16_t next = fat16_get_entry(cl);
        if (next >= 0xFFF8 || next == FAT16_EOC || next < 2) return 0;
        cl = next;
    }

    uint32_t done = 0;
    uint8_t sec[FAT16_SECTOR_SIZE];
    while (done < len && cl >= 2 && cl < 0xFFF8) {
        for (uint8_t s = 0; s < g_fat.sectors_per_cluster && done < len; s++) {
            uint32_t sector_off = (uint32_t)s * FAT16_SECTOR_SIZE;
            uint32_t lba = cluster_to_lba(cl) + s;
            if (ata_read_sector(lba, sec) < 0) return (int)done;

            if (in_cluster >= sector_off + FAT16_SECTOR_SIZE) {
                continue;
            }

            uint32_t start = 0;
            if (in_cluster > sector_off) start = in_cluster - sector_off;
            uint32_t avail = FAT16_SECTOR_SIZE - start;
            uint32_t need = len - done;
            uint32_t take = (avail < need) ? avail : need;

            memcpy(out + done, sec + start, take);
            done += take;
        }
        in_cluster = 0;
        cl = fat16_get_entry(cl);
    }

    return (int)done;
}

static int fat16_vfs_open(const char *path, int flags) {
    if (!g_fat.mounted || !path) return -1;

    int access = flags & 0x3;
    if (!(access == O_RDONLY || access == O_WRONLY || access == O_RDWR)) {
        return -1;
    }

    fat16_dirent_t de;
    uint32_t de_lba = 0, free_lba = 0;
    uint16_t de_off = 0, free_off = 0;
    uint8_t name83[11];

    int found = (fat16_lookup_root(path, &de, &de_lba, &de_off, name83, &free_lba, &free_off) == 0);

    if (!found) {
        if (!(flags & O_CREAT)) return -1;
        if (free_lba == 0) return -1;

        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(free_lba, sec) < 0) return -1;
        fat16_dirent_t *nde = (fat16_dirent_t *)(sec + free_off);
        memset(nde, 0, sizeof(*nde));
        memcpy(nde->name, name83, 11);
        nde->attr = FAT16_ATTR_ARCHIVE;
        nde->first_cluster_lo = 0;
        nde->file_size = 0;
        if (ata_write_sector(free_lba, sec) < 0) return -1;

        de = *nde;
        de_lba = free_lba;
        de_off = free_off;
    }

    if (de.attr & FAT16_ATTR_DIR) return -1;

    // Truncate regular file when requested and writable.
    if ((flags & O_TRUNC) && access != O_RDONLY) {
        if (de.first_cluster_lo >= 2) {
            if (fat16_free_chain(de.first_cluster_lo) < 0) return -1;
        }
        de.first_cluster_lo = 0;
        de.file_size = 0;

        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(de_lba, sec) < 0) return -1;
        fat16_dirent_t *wde = (fat16_dirent_t *)(sec + de_off);
        wde->first_cluster_lo = 0;
        wde->file_size = 0;
        if (ata_write_sector(de_lba, sec) < 0) return -1;
    }

    int h = -1;
    for (int i = 0; i < FAT16_MAX_OPEN; i++) {
        if (!g_open[i].in_use) {
            h = i;
            break;
        }
    }
    if (h < 0) return -1;

    g_open[h].in_use = 1;
    g_open[h].flags = flags;
    g_open[h].first_cluster = de.first_cluster_lo;
    g_open[h].size = de.file_size;
    g_open[h].pos = 0;
    g_open[h].attr = de.attr;
    g_open[h].dirent_lba = de_lba;
    g_open[h].dirent_off = de_off;
    return h;
}

static int fat16_vfs_read(int handle, void *buf, uint32_t len) {
    if (!g_fat.mounted || !buf || handle < 0 || handle >= FAT16_MAX_OPEN) return -1;
    if (!g_open[handle].in_use) return -1;
    fat16_open_t *f = &g_open[handle];

    int access = f->flags & 0x3;
    if (access == O_WRONLY) return -1;

    if (f->pos >= f->size) return 0;
    uint32_t remain = f->size - f->pos;
    if (len > remain) len = remain;

    int n = fat16_read_file(f->first_cluster, f->pos, buf, len);
    if (n > 0) f->pos += (uint32_t)n;
    return n;
}

static int fat16_vfs_write(int handle, const void *buf, uint32_t len) {
    if (!g_fat.mounted || !buf || handle < 0 || handle >= FAT16_MAX_OPEN) return -1;
    if (!g_open[handle].in_use) return -1;

    fat16_open_t *f = &g_open[handle];
    int access = f->flags & 0x3;
    if (access == O_RDONLY) return -1;
    if (len == 0) return 0;

    uint32_t cluster_size = (uint32_t)g_fat.sectors_per_cluster * FAT16_SECTOR_SIZE;
    uint32_t done = 0;

    while (done < len) {
        uint32_t abs_pos = f->pos + done;
        uint32_t cl_idx = abs_pos / cluster_size;
        uint32_t in_cl = abs_pos % cluster_size;

        uint16_t cl;
        if (fat16_ensure_cluster_for_index(f, cl_idx, &cl) < 0) break;

        uint32_t sec_idx = in_cl / FAT16_SECTOR_SIZE;
        uint32_t sec_off = in_cl % FAT16_SECTOR_SIZE;
        uint32_t lba = cluster_to_lba(cl) + sec_idx;

        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(lba, sec) < 0) break;

        uint32_t space = FAT16_SECTOR_SIZE - sec_off;
        uint32_t need = len - done;
        uint32_t take = (space < need) ? space : need;

        memcpy(sec + sec_off, (const uint8_t *)buf + done, take);
        if (ata_write_sector(lba, sec) < 0) break;

        done += take;
    }

    f->pos += done;
    if (f->pos > f->size) {
        f->size = f->pos;
    }

    if (done > 0) {
        if (fat16_update_dirent(f) < 0) return -1;
    }

    return (int)done;
}

static int fat16_vfs_close(int handle) {
    if (handle < 0 || handle >= FAT16_MAX_OPEN) return -1;
    if (!g_open[handle].in_use) return -1;
    memset(&g_open[handle], 0, sizeof(g_open[handle]));
    return 0;
}

static int fat16_vfs_seek(int handle, int offset, int whence) {
    if (handle < 0 || handle >= FAT16_MAX_OPEN) return -1;
    if (!g_open[handle].in_use) return -1;

    fat16_open_t *f = &g_open[handle];
    int pos;
    switch (whence) {
        case SEEK_SET: pos = offset; break;
        case SEEK_CUR: pos = (int)f->pos + offset; break;
        case SEEK_END: pos = (int)f->size + offset; break;
        default: return -1;
    }
    if (pos < 0) pos = 0;
    if ((uint32_t)pos > f->size) pos = (int)f->size;
    f->pos = (uint32_t)pos;
    return pos;
}

static int fat16_vfs_stat(const char *path, vfs_stat_t *st) {
    if (!g_fat.mounted || !path || !st) return -1;

    fat16_dirent_t de;
    if (fat16_lookup_root(path, &de, NULL, NULL, NULL, NULL, NULL) < 0) return -1;

    st->size = de.file_size;
    st->type = (de.attr & FAT16_ATTR_DIR) ? VFS_DIR : VFS_FILE;
    return 0;
}

static int fat16_vfs_readdir(const char *path __attribute__((unused)),
                             int index, char *buf, uint32_t size) {
    if (!g_fat.mounted || !buf || size == 0 || index < 0) return 0;

    uint8_t sec[FAT16_SECTOR_SIZE];
    int vis = 0;
    for (uint32_t s = 0; s < g_fat.root_dir_sectors; s++) {
        if (ata_read_sector(g_fat.root_start_lba + s, sec) < 0) return 0;

        for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
            fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);
            if (de->name[0] == 0x00) return 0;
            if (de->name[0] == 0xE5) continue;
            if (de->attr == FAT16_ATTR_LFN) continue;
            if (de->attr & FAT16_ATTR_VOLUMEID) continue;

            if (vis == index) {
                char name[13];
                fat16_dirent_name_to_string(de, name);
                size_t n = strlen(name);
                if (n >= size) n = size - 1;
                memcpy(buf, name, n);
                buf[n] = '\0';
                return (int)(n + 1);
            }
            vis++;
        }
    }
    return 0;
}

static int fat16_vfs_unlink(const char *path) {
    if (!g_fat.mounted || !path) return -1;

    fat16_dirent_t de;
    uint32_t de_lba = 0;
    uint16_t de_off = 0;
    if (fat16_lookup_root(path, &de, &de_lba, &de_off, NULL, NULL, NULL) < 0) {
        return -1;
    }
    if (de.attr & FAT16_ATTR_DIR) return -1;

    if (de.first_cluster_lo >= 2) {
        if (fat16_free_chain(de.first_cluster_lo) < 0) return -1;
    }

    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(de_lba, sec) < 0) return -1;
    fat16_dirent_t *wde = (fat16_dirent_t *)(sec + de_off);
    wde->name[0] = 0xE5;
    if (ata_write_sector(de_lba, sec) < 0) return -1;

    return 0;
}

static const vfs_fs_ops_t fat16_ops = {
    .name = "fat16",
    .open = fat16_vfs_open,
    .read = fat16_vfs_read,
    .write = fat16_vfs_write,
    .close = fat16_vfs_close,
    .seek = fat16_vfs_seek,
    .stat = fat16_vfs_stat,
    .readdir = fat16_vfs_readdir,
    .unlink = fat16_vfs_unlink,
};

static int fat16_try_mount_at(uint32_t part_lba) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(part_lba, sec) < 0) return -1;

    fat16_bpb_t *b = (fat16_bpb_t *)sec;
    if (b->bytes_per_sector != FAT16_SECTOR_SIZE) return -1;
    if (b->sectors_per_cluster == 0) return -1;
    if (b->fat_count == 0) return -1;
    if (b->root_entry_count == 0) return -1;
    if (b->sectors_per_fat_16 == 0) return -1;

    uint32_t total = b->total_sectors_16 ? b->total_sectors_16 : b->total_sectors_32;
    if (total == 0) return -1;

    uint32_t root_secs = ((uint32_t)b->root_entry_count * 32u + (FAT16_SECTOR_SIZE - 1)) /
                         FAT16_SECTOR_SIZE;
    uint32_t data_secs = total - (b->reserved_sector_count +
                                  ((uint32_t)b->fat_count * b->sectors_per_fat_16) +
                                  root_secs);
    uint32_t clusters = data_secs / b->sectors_per_cluster;

    // FAT16 range
    if (clusters < 4085 || clusters >= 65525) return -1;

    memset(&g_fat, 0, sizeof(g_fat));
    g_fat.mounted = 1;
    g_fat.part_lba = part_lba;
    g_fat.bytes_per_sector = b->bytes_per_sector;
    g_fat.sectors_per_cluster = b->sectors_per_cluster;
    g_fat.root_entry_count = b->root_entry_count;
    g_fat.total_sectors = total;
    g_fat.sectors_per_fat = b->sectors_per_fat_16;
    g_fat.fat_count = b->fat_count;
    g_fat.fat_start_lba = part_lba + b->reserved_sector_count;
    g_fat.root_start_lba = g_fat.fat_start_lba + ((uint32_t)b->fat_count * b->sectors_per_fat_16);
    g_fat.root_dir_sectors = root_secs;
    g_fat.data_start_lba = g_fat.root_start_lba + root_secs;
    g_fat.cluster_count = clusters;

    memset(g_open, 0, sizeof(g_open));

    printf("[fat16] mounted at LBA %d (spc=%d, root_entries=%d)\n",
           g_fat.part_lba, g_fat.sectors_per_cluster, g_fat.root_entry_count);
    return 0;
}

int fat16_init(void) {
    memset(&g_fat, 0, sizeof(g_fat));
    memset(g_open, 0, sizeof(g_open));

    if (ata_pio_init() < 0) {
        printf("[fat16] ATA PIO disk not found\n");
        return -1;
    }

    // Try MBR partition first.
    uint8_t mbr[FAT16_SECTOR_SIZE];
    if (ata_read_sector(0, mbr) == 0 && mbr[510] == 0x55 && mbr[511] == 0xAA) {
        mbr_part_t *p = (mbr_part_t *)(mbr + 446);
        for (int i = 0; i < 4; i++) {
            if (is_fat16_part_type(p[i].type) && p[i].sector_count > 0) {
                if (fat16_try_mount_at(p[i].lba_first) == 0) {
                    return 0;
                }
            }
        }
    }

    // Fallback: superfloppy FAT16 directly at LBA0
    if (fat16_try_mount_at(0) == 0) {
        return 0;
    }

    printf("[fat16] no FAT16 volume found\n");
    return -1;
}

const vfs_fs_ops_t *fat16_get_ops(void) {
    return &fat16_ops;
}
