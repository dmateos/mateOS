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
#define FAT16_ATTR_LFN      0x0F

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
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t root_entry_count;
} fat16_state_t;

typedef struct {
    int in_use;
    uint16_t first_cluster;
    uint32_t size;
    uint32_t pos;
    uint8_t attr;
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

static int str_caseeq(const char *a, const char *b) {
    while (*a && *b) {
        if (upper_ascii(*a) != upper_ascii(*b)) return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int ata_read_sector(uint32_t lba, uint8_t *out) {
    return ata_pio_read(lba, 1, out);
}

static uint32_t cluster_to_lba(uint16_t cluster) {
    return g_fat.data_start_lba +
           ((uint32_t)(cluster - 2) * g_fat.sectors_per_cluster);
}

static uint16_t fat16_next_cluster(uint16_t cluster) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_sec = g_fat.fat_start_lba + (fat_offset / FAT16_SECTOR_SIZE);
    uint32_t ent_off = fat_offset % FAT16_SECTOR_SIZE;

    if (ata_read_sector(fat_sec, sec) < 0) return 0xFFFF;
    return *(uint16_t *)(sec + ent_off);
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

static int fat16_lookup_root(const char *path, fat16_dirent_t *out) {
    if (!g_fat.mounted || !path || !out) return -1;

    const char *name = fat16_basename(path);
    if (!name || !*name) return -1;

    uint8_t sec[FAT16_SECTOR_SIZE];
    uint32_t seen = 0;
    for (uint32_t s = 0; s < g_fat.root_dir_sectors; s++) {
        if (ata_read_sector(g_fat.root_start_lba + s, sec) < 0) return -1;

        for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
            fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);
            if (de->name[0] == 0x00) return -1;
            if (de->name[0] == 0xE5) continue;
            if (de->attr == FAT16_ATTR_LFN) continue;
            if (de->attr & FAT16_ATTR_VOLUMEID) continue;

            char disp[13];
            fat16_dirent_name_to_string(de, disp);
            if (str_caseeq(disp, name)) {
                *out = *de;
                (void)seen;
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

static int fat16_read_file(uint16_t first_cluster, uint32_t pos, void *buf, uint32_t len) {
    if (len == 0) return 0;

    // Empty file
    if (first_cluster < 2) return 0;

    uint8_t *out = (uint8_t *)buf;
    uint32_t cluster_size = (uint32_t)g_fat.sectors_per_cluster * FAT16_SECTOR_SIZE;
    uint32_t skip_clusters = pos / cluster_size;
    uint32_t in_cluster = pos % cluster_size;

    uint16_t cl = first_cluster;
    for (uint32_t i = 0; i < skip_clusters; i++) {
        cl = fat16_next_cluster(cl);
        if (cl >= 0xFFF8 || cl == 0xFFFF || cl < 2) return 0;
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
        cl = fat16_next_cluster(cl);
    }

    return (int)done;
}

static int fat16_vfs_open(const char *path, int flags) {
    if (!g_fat.mounted || !path) return -1;

    // Read-only for now.
    if (flags != O_RDONLY) return -1;

    fat16_dirent_t de;
    if (fat16_lookup_root(path, &de) < 0) return -1;
    if (de.attr & FAT16_ATTR_DIR) return -1;

    int h = -1;
    for (int i = 0; i < FAT16_MAX_OPEN; i++) {
        if (!g_open[i].in_use) {
            h = i;
            break;
        }
    }
    if (h < 0) return -1;

    g_open[h].in_use = 1;
    g_open[h].first_cluster = de.first_cluster_lo;
    g_open[h].size = de.file_size;
    g_open[h].pos = 0;
    g_open[h].attr = de.attr;
    return h;
}

static int fat16_vfs_read(int handle, void *buf, uint32_t len) {
    if (!g_fat.mounted || !buf || handle < 0 || handle >= FAT16_MAX_OPEN) return -1;
    if (!g_open[handle].in_use) return -1;
    fat16_open_t *f = &g_open[handle];

    if (f->pos >= f->size) return 0;
    uint32_t remain = f->size - f->pos;
    if (len > remain) len = remain;

    int n = fat16_read_file(f->first_cluster, f->pos, buf, len);
    if (n > 0) f->pos += (uint32_t)n;
    return n;
}

static int fat16_vfs_write(int handle __attribute__((unused)),
                           const void *buf __attribute__((unused)),
                           uint32_t len __attribute__((unused))) {
    return -1;
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
    if (fat16_lookup_root(path, &de) < 0) return -1;

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

static const vfs_fs_ops_t fat16_ops = {
    .name = "fat16",
    .open = fat16_vfs_open,
    .read = fat16_vfs_read,
    .write = fat16_vfs_write,
    .close = fat16_vfs_close,
    .seek = fat16_vfs_seek,
    .stat = fat16_vfs_stat,
    .readdir = fat16_vfs_readdir,
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
    g_fat.fat_start_lba = part_lba + b->reserved_sector_count;
    g_fat.root_start_lba = g_fat.fat_start_lba + ((uint32_t)b->fat_count * b->sectors_per_fat_16);
    g_fat.root_dir_sectors = root_secs;
    g_fat.data_start_lba = g_fat.root_start_lba + root_secs;

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
