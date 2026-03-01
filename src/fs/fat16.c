#include "fat16.h"
#include "drivers/ata_pio.h"
#include "lib.h"
#include "vfs.h"

#define FAT16_SECTOR_SIZE 512
#define FAT16_MAX_OPEN 16
#define FAT16_MAX_PATH 64

#define FAT16_ATTR_READONLY 0x01
#define FAT16_ATTR_HIDDEN 0x02
#define FAT16_ATTR_SYSTEM 0x04
#define FAT16_ATTR_VOLUMEID 0x08
#define FAT16_ATTR_DIR 0x10
#define FAT16_ATTR_ARCHIVE 0x20
#define FAT16_ATTR_LFN 0x0F

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

// Represents the location of a directory (root dir vs subdir cluster chain).
// cluster==0 means the fixed root directory region.
typedef struct {
    uint16_t cluster; // 0 = root dir, >= 2 = first cluster of subdir
} fat16_dir_loc_t;

static fat16_state_t g_fat;
static fat16_open_t g_open[FAT16_MAX_OPEN];

// Forward declarations for ATA helpers
static int ata_read_sector(uint32_t lba, uint8_t *out);
static int ata_write_sector(uint32_t lba, const uint8_t *in);

// ---------------------------------------------------------------------------
// FAT sector cache — avoids re-reading the same FAT sector on every cluster
// chain lookup. Each entry caches one 512-byte FAT sector.
// ---------------------------------------------------------------------------
#define FAT_CACHE_SIZE 8
static struct {
    uint32_t lba;  // 0 = unused
    uint8_t data[FAT16_SECTOR_SIZE];
} fat_cache[FAT_CACHE_SIZE];
static int fat_cache_next; // round-robin eviction index

static void fat_cache_invalidate(void) {
    for (int i = 0; i < FAT_CACHE_SIZE; i++)
        fat_cache[i].lba = 0;
    fat_cache_next = 0;
}

static uint8_t *fat_cache_get(uint32_t lba) {
    // Check cache
    for (int i = 0; i < FAT_CACHE_SIZE; i++) {
        if (fat_cache[i].lba == lba)
            return fat_cache[i].data;
    }
    // Miss — read and cache
    int slot = fat_cache_next;
    fat_cache_next = (fat_cache_next + 1) % FAT_CACHE_SIZE;
    if (ata_read_sector(lba, fat_cache[slot].data) < 0)
        return NULL;
    fat_cache[slot].lba = lba;
    return fat_cache[slot].data;
}

// Invalidate a specific LBA (after writes to FAT)
static void fat_cache_evict(uint32_t lba) {
    for (int i = 0; i < FAT_CACHE_SIZE; i++) {
        if (fat_cache[i].lba == lba)
            fat_cache[i].lba = 0;
    }
}

static int is_fat16_part_type(uint8_t type) {
    return (type == 0x04 || type == 0x06 || type == 0x0E);
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z')
        return (char)(c - ('a' - 'A'));
    return c;
}

static int is_83_char(char c) {
    if (c >= 'A' && c <= 'Z')
        return 1;
    if (c >= '0' && c <= '9')
        return 1;
    if (c == '_' || c == '$' || c == '~' || c == '-' || c == '!')
        return 1;
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
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_sec = g_fat.fat_start_lba + (fat_offset / FAT16_SECTOR_SIZE);
    uint32_t ent_off = fat_offset % FAT16_SECTOR_SIZE;

    uint8_t *sec = fat_cache_get(fat_sec);
    if (!sec)
        return FAT16_EOC;
    return read_u16(sec + ent_off);
}

static int fat16_set_entry(uint16_t cluster, uint16_t value) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_rel_sec = fat_offset / FAT16_SECTOR_SIZE;
    uint32_t ent_off = fat_offset % FAT16_SECTOR_SIZE;

    // Mirror to all FAT copies
    for (uint8_t fat_i = 0; fat_i < g_fat.fat_count; fat_i++) {
        uint32_t fat_sec =
            g_fat.fat_start_lba + fat_i * g_fat.sectors_per_fat + fat_rel_sec;
        if (ata_read_sector(fat_sec, sec) < 0)
            return -1;
        write_u16(sec + ent_off, value);
        if (ata_write_sector(fat_sec, sec) < 0)
            return -1;
        // Evict cached copy since we wrote new data
        fat_cache_evict(fat_sec);
    }

    return 0;
}

static int fat16_alloc_cluster(uint16_t *out_cluster) {
    if (!out_cluster)
        return -1;

    for (uint32_t c = 2; c < g_fat.cluster_count + 2; c++) {
        uint16_t v = fat16_get_entry((uint16_t)c);
        if (v == 0x0000) {
            if (fat16_set_entry((uint16_t)c, FAT16_EOC) < 0)
                return -1;

            // Zero the newly allocated cluster on disk
            uint8_t zero[FAT16_SECTOR_SIZE];
            memset(zero, 0, sizeof(zero));
            uint32_t lba = cluster_to_lba((uint16_t)c);
            for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
                if (ata_write_sector(lba + s, zero) < 0)
                    return -1;
            }

            *out_cluster = (uint16_t)c;
            return 0;
        }
    }
    return -1;
}

static int fat16_free_chain(uint16_t first) {
    uint16_t c = first;
    // Limit iterations to total cluster count to prevent infinite loops
    // on corrupt FAT chains with cycles (e.g. A->B->C->A).
    uint32_t max_iter = g_fat.cluster_count + 2;
    uint32_t iter = 0;
    while (c >= 2 && c < 0xFFF8 && iter < max_iter) {
        uint16_t next = fat16_get_entry(c);
        if (fat16_set_entry(c, 0x0000) < 0)
            return -1;
        if (next == c)
            break;
        c = next;
        iter++;
    }
    return 0;
}

static int fat16_ensure_cluster_for_index(fat16_open_t *f, uint32_t idx,
                                          uint16_t *out_cluster) {
    if (!f || !out_cluster)
        return -1;

    if (f->first_cluster < 2) {
        uint16_t n;
        if (fat16_alloc_cluster(&n) < 0)
            return -1;
        f->first_cluster = n;
    }

    uint16_t c = f->first_cluster;
    for (uint32_t i = 0; i < idx; i++) {
        uint16_t next = fat16_get_entry(c);
        if (next >= 0xFFF8 || next < 2) {
            uint16_t n;
            if (fat16_alloc_cluster(&n) < 0)
                return -1;
            if (fat16_set_entry(c, n) < 0)
                return -1;
            if (fat16_set_entry(n, FAT16_EOC) < 0)
                return -1;
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
        if (de->name[i] == ' ')
            break;
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
            if (de->name[i] == ' ')
                break;
            out[p++] = (char)de->name[i];
        }
    }
    out[p] = '\0';
}

static int fat16_name_to_83(const char *in, uint8_t out[11]) {
    if (!in || !*in)
        return -1;

    for (int i = 0; i < 11; i++)
        out[i] = ' ';

    int i = 0;
    int base_len = 0;
    while (in[i] && in[i] != '.') {
        char c = upper_ascii(in[i]);
        if (!is_83_char(c))
            return -1;
        if (base_len >= 8)
            return -1;
        out[base_len++] = (uint8_t)c;
        i++;
    }
    if (base_len == 0)
        return -1;

    if (in[i] == '.') {
        i++;
        int ext_len = 0;
        while (in[i]) {
            char c = upper_ascii(in[i]);
            if (!is_83_char(c))
                return -1;
            if (ext_len >= 3)
                return -1;
            out[8 + ext_len] = (uint8_t)c;
            ext_len++;
            i++;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Directory-aware lookup: works for both root dir and subdirectories
// ---------------------------------------------------------------------------

// Search a directory for a name83 entry. For root dir (dir.cluster==0),
// scans the fixed root directory sectors. For subdirs, follows the cluster
// chain. Returns 0 on match, -1 on not found. Optionally returns first free
// slot.
// Helper: scan a sector buffer for a matching 8.3 name or free slot.
// Returns 1 if match found, -1 if end-of-directory (0x00 marker), 0 to keep scanning.
static int fat16_scan_dir_sector(const uint8_t *sec, uint32_t lba,
                                 const uint8_t name83[11],
                                 fat16_dirent_t *out, uint32_t *out_lba,
                                 uint16_t *out_off, uint32_t *free_lba,
                                 uint16_t *free_off) {
    for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
        const fat16_dirent_t *de = (const fat16_dirent_t *)(sec + off);

        if (de->name[0] == 0x00) {
            if (free_lba && *free_lba == 0) {
                *free_lba = lba;
                *free_off = (uint16_t)off;
            }
            return -1; // end of directory
        }
        if (de->name[0] == 0xE5) {
            if (free_lba && *free_lba == 0) {
                *free_lba = lba;
                *free_off = (uint16_t)off;
            }
            continue;
        }
        if (de->attr == FAT16_ATTR_LFN)
            continue;
        if (de->attr & FAT16_ATTR_VOLUMEID)
            continue;

        if (memcmp(de->name, name83, 11) == 0) {
            if (out)
                *out = *de;
            if (out_lba)
                *out_lba = lba;
            if (out_off)
                *out_off = (uint16_t)off;
            return 1; // found
        }
    }
    return 0; // keep scanning
}

static int fat16_lookup_in_dir(fat16_dir_loc_t dir, const uint8_t name83[11],
                               fat16_dirent_t *out, uint32_t *out_lba,
                               uint16_t *out_off, uint32_t *free_lba,
                               uint16_t *free_off) {
    if (!g_fat.mounted || !name83)
        return -1;

    if (dir.cluster == 0) {
        // Root directory: fixed LBA region — bulk-read up to 8 sectors
        uint8_t bulk[FAT16_SECTOR_SIZE * 8];
        uint32_t s = 0;
        while (s < g_fat.root_dir_sectors) {
            uint32_t batch = g_fat.root_dir_sectors - s;
            if (batch > 8) batch = 8;
            uint32_t base_lba = g_fat.root_start_lba + s;
            if (ata_pio_read(base_lba, (uint8_t)batch, bulk) < 0)
                return -1;
            for (uint32_t b = 0; b < batch; b++) {
                int rc = fat16_scan_dir_sector(
                    bulk + b * FAT16_SECTOR_SIZE, base_lba + b,
                    name83, out, out_lba, out_off, free_lba, free_off);
                if (rc == 1) return 0;   // found
                if (rc == -1) return -1;  // end of dir
            }
            s += batch;
        }
    } else {
        // Subdirectory: follow cluster chain, bulk-read entire cluster
        uint8_t cbuf[FAT16_SECTOR_SIZE * 8]; // up to 8 sectors per cluster
        int can_bulk = (g_fat.sectors_per_cluster <= 8);
        uint16_t cl = dir.cluster;
        while (cl >= 2 && cl < 0xFFF8) {
            uint32_t base_lba = cluster_to_lba(cl);
            if (can_bulk) {
                if (ata_pio_read(base_lba, g_fat.sectors_per_cluster, cbuf) < 0)
                    return -1;
                for (uint8_t b = 0; b < g_fat.sectors_per_cluster; b++) {
                    int rc = fat16_scan_dir_sector(
                        cbuf + b * FAT16_SECTOR_SIZE, base_lba + b,
                        name83, out, out_lba, out_off, free_lba, free_off);
                    if (rc == 1) return 0;
                    if (rc == -1) return -1;
                }
            } else {
                uint8_t sec[FAT16_SECTOR_SIZE];
                for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
                    if (ata_read_sector(base_lba + s, sec) < 0)
                        return -1;
                    int rc = fat16_scan_dir_sector(
                        sec, base_lba + s,
                        name83, out, out_lba, out_off, free_lba, free_off);
                    if (rc == 1) return 0;
                    if (rc == -1) return -1;
                }
            }
            cl = fat16_get_entry(cl);
        }
    }
    return -1;
}

// Resolve an absolute path like "/foo/bar/baz.c" into:
//   - parent_dir: the directory containing the final component
//   - final_de: the dirent of the final component (if found)
//   - name83: the 8.3 name of the final component
//   - free slot in parent (for O_CREAT / mkdir)
// Returns 0 if the final entry was found, -1 if not (parent may still be
// valid). If the path is "/" or empty, returns -2 (refers to root dir itself).
static int fat16_resolve_path(const char *path, fat16_dir_loc_t *parent_dir,
                              fat16_dirent_t *final_de, uint32_t *de_lba,
                              uint16_t *de_off, uint8_t name83[11],
                              uint32_t *free_lba, uint16_t *free_off) {
    if (!g_fat.mounted || !path)
        return -1;

    // Skip leading slash
    while (*path == '/')
        path++;
    if (*path == '\0')
        return -2; // path is just "/"

    fat16_dir_loc_t cur_dir = {.cluster = 0}; // start at root

    // Walk path components separated by '/'
    while (1) {
        // Find end of current component
        const char *end = path;
        while (*end && *end != '/')
            end++;
        int comp_len = (int)(end - path);
        if (comp_len == 0) {
            path = end + 1;
            continue;
        }

        // Extract component name
        char comp[13];
        if (comp_len >= (int)sizeof(comp))
            return -1; // name too long
        memcpy(comp, path, (size_t)comp_len);
        comp[comp_len] = '\0';

        // Convert to 8.3
        uint8_t c83[11];
        if (fat16_name_to_83(comp, c83) < 0)
            return -1;

        // Is this the last component?
        const char *rest = end;
        while (*rest == '/')
            rest++;
        int is_last = (*rest == '\0');

        if (is_last) {
            // This is the final component — look it up in cur_dir
            if (parent_dir)
                *parent_dir = cur_dir;
            if (name83)
                memcpy(name83, c83, 11);
            if (free_lba)
                *free_lba = 0;
            if (free_off)
                *free_off = 0;
            return fat16_lookup_in_dir(cur_dir, c83, final_de, de_lba, de_off,
                                       free_lba, free_off);
        } else {
            // Intermediate component — must be a directory
            fat16_dirent_t de;
            if (fat16_lookup_in_dir(cur_dir, c83, &de, NULL, NULL, NULL, NULL) <
                0) {
                return -1; // intermediate dir not found
            }
            if (!(de.attr & FAT16_ATTR_DIR))
                return -1; // not a directory
            cur_dir.cluster = de.first_cluster_lo;
            path = rest;
        }
    }
}

// Resolve a path to a directory location. If path is "/" or empty, returns
// root. If path points to a directory entry, returns its cluster. Returns 0 on
// success.
static int fat16_resolve_dir(const char *path, fat16_dir_loc_t *out_dir) {
    if (!g_fat.mounted || !path || !out_dir)
        return -1;

    // Skip leading slash
    const char *p = path;
    while (*p == '/')
        p++;
    if (*p == '\0') {
        out_dir->cluster = 0; // root directory
        return 0;
    }

    fat16_dir_loc_t parent;
    fat16_dirent_t de;
    int rc =
        fat16_resolve_path(path, &parent, &de, NULL, NULL, NULL, NULL, NULL);
    if (rc == -2) {
        out_dir->cluster = 0;
        return 0;
    }
    if (rc < 0)
        return -1;
    if (!(de.attr & FAT16_ATTR_DIR))
        return -1;
    out_dir->cluster = de.first_cluster_lo;
    return 0;
}

static int fat16_update_dirent(fat16_open_t *f) {
    if (!f || f->dirent_lba == 0)
        return -1;
    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(f->dirent_lba, sec) < 0)
        return -1;

    fat16_dirent_t *de = (fat16_dirent_t *)(sec + f->dirent_off);
    de->first_cluster_lo = f->first_cluster;
    de->file_size = f->size;
    de->attr = f->attr;

    if (ata_write_sector(f->dirent_lba, sec) < 0)
        return -1;
    return 0;
}

// Read from a cluster chain. Uses multi-sector ATA reads when possible:
// reads an entire cluster (sectors_per_cluster sectors) in one ATA command
// instead of one sector at a time.
static int fat16_read_file(uint16_t first_cluster, uint32_t pos, void *buf,
                           uint32_t len) {
    if (len == 0)
        return 0;

    if (first_cluster < 2)
        return 0;

    uint8_t *out = (uint8_t *)buf;
    uint32_t cluster_size =
        (uint32_t)g_fat.sectors_per_cluster * FAT16_SECTOR_SIZE;
    uint32_t skip_clusters = pos / cluster_size;
    uint32_t in_cluster = pos % cluster_size;

    uint16_t cl = first_cluster;
    for (uint32_t i = 0; i < skip_clusters; i++) {
        uint16_t next = fat16_get_entry(cl);
        if (next >= 0xFFF8 || next == FAT16_EOC || next < 2)
            return 0;
        cl = next;
    }

    uint32_t done = 0;
    // Cluster buffer for multi-sector reads (max 64 sectors/cluster = 32KB,
    // but typical is 1-8 sectors). Stack buffer for small clusters, else
    // fall back to per-sector reads.
    uint8_t cluster_buf[FAT16_SECTOR_SIZE * 8]; // up to 4KB cluster
    int can_bulk = (g_fat.sectors_per_cluster <= 8);

    while (done < len && cl >= 2 && cl < 0xFFF8) {
        uint32_t lba = cluster_to_lba(cl);

        if (can_bulk && in_cluster == 0 && (len - done) >= cluster_size) {
            // Fast path: read entire cluster directly into output buffer
            if (ata_pio_read(lba, g_fat.sectors_per_cluster, out + done) < 0)
                return (int)done;
            done += cluster_size;
        } else if (can_bulk) {
            // Partial cluster: bulk read into temp buffer, copy needed portion
            if (ata_pio_read(lba, g_fat.sectors_per_cluster, cluster_buf) < 0)
                return (int)done;
            uint32_t avail = cluster_size - in_cluster;
            uint32_t need = len - done;
            uint32_t take = (avail < need) ? avail : need;
            memcpy(out + done, cluster_buf + in_cluster, take);
            done += take;
        } else {
            // Large cluster: read sector by sector (rare)
            for (uint8_t s = 0; s < g_fat.sectors_per_cluster && done < len;
                 s++) {
                uint32_t sector_off = (uint32_t)s * FAT16_SECTOR_SIZE;
                if (in_cluster >= sector_off + FAT16_SECTOR_SIZE)
                    continue;

                uint8_t sec[FAT16_SECTOR_SIZE];
                if (ata_read_sector(lba + s, sec) < 0)
                    return (int)done;

                uint32_t start = 0;
                if (in_cluster > sector_off)
                    start = in_cluster - sector_off;
                uint32_t avail = FAT16_SECTOR_SIZE - start;
                uint32_t need = len - done;
                uint32_t take = (avail < need) ? avail : need;
                memcpy(out + done, sec + start, take);
                done += take;
            }
        }
        in_cluster = 0;
        cl = fat16_get_entry(cl);
    }

    return (int)done;
}

// ---------------------------------------------------------------------------
// VFS operations — now path-aware with subdirectory support
// ---------------------------------------------------------------------------

static int fat16_vfs_open(const char *path, int flags) {
    if (!g_fat.mounted || !path)
        return -1;

    int access = flags & 0x3;
    if (!(access == O_RDONLY || access == O_WRONLY || access == O_RDWR)) {
        return -1;
    }

    fat16_dir_loc_t parent;
    fat16_dirent_t de;
    uint32_t de_lba = 0, free_lba = 0;
    uint16_t de_off = 0, free_off = 0;
    uint8_t name83[11];

    int rc = fat16_resolve_path(path, &parent, &de, &de_lba, &de_off, name83,
                                &free_lba, &free_off);
    if (rc == -2)
        return -1; // can't open root dir as file

    int found = (rc == 0);

    if (!found) {
        if (!(flags & O_CREAT))
            return -1;
        if (free_lba == 0)
            return -1;

        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(free_lba, sec) < 0)
            return -1;
        fat16_dirent_t *nde = (fat16_dirent_t *)(sec + free_off);
        memset(nde, 0, sizeof(*nde));
        memcpy(nde->name, name83, 11);
        nde->attr = FAT16_ATTR_ARCHIVE;
        nde->first_cluster_lo = 0;
        nde->file_size = 0;
        if (ata_write_sector(free_lba, sec) < 0)
            return -1;

        de = *nde;
        de_lba = free_lba;
        de_off = free_off;
    }

    if (de.attr & FAT16_ATTR_DIR)
        return -1;

    // Truncate regular file when requested and writable.
    if ((flags & O_TRUNC) && access != O_RDONLY) {
        if (de.first_cluster_lo >= 2) {
            if (fat16_free_chain(de.first_cluster_lo) < 0)
                return -1;
        }
        de.first_cluster_lo = 0;
        de.file_size = 0;

        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(de_lba, sec) < 0)
            return -1;
        fat16_dirent_t *wde = (fat16_dirent_t *)(sec + de_off);
        wde->first_cluster_lo = 0;
        wde->file_size = 0;
        if (ata_write_sector(de_lba, sec) < 0)
            return -1;
    }

    int h = -1;
    for (int i = 0; i < FAT16_MAX_OPEN; i++) {
        if (!g_open[i].in_use) {
            h = i;
            break;
        }
    }
    if (h < 0)
        return -1;

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
    if (!g_fat.mounted || !buf || handle < 0 || handle >= FAT16_MAX_OPEN)
        return -1;
    if (!g_open[handle].in_use)
        return -1;
    fat16_open_t *f = &g_open[handle];

    int access = f->flags & 0x3;
    if (access == O_WRONLY)
        return -1;

    if (f->pos >= f->size)
        return 0;
    uint32_t remain = f->size - f->pos;
    if (len > remain)
        len = remain;

    int n = fat16_read_file(f->first_cluster, f->pos, buf, len);
    if (n > 0)
        f->pos += (uint32_t)n;
    return n;
}

static int fat16_vfs_write(int handle, const void *buf, uint32_t len) {
    if (!g_fat.mounted || !buf || handle < 0 || handle >= FAT16_MAX_OPEN)
        return -1;
    if (!g_open[handle].in_use)
        return -1;

    fat16_open_t *f = &g_open[handle];
    int access = f->flags & 0x3;
    if (access == O_RDONLY)
        return -1;
    if (len == 0)
        return 0;

    uint32_t cluster_size =
        (uint32_t)g_fat.sectors_per_cluster * FAT16_SECTOR_SIZE;
    uint32_t done = 0;

    while (done < len) {
        uint32_t abs_pos = f->pos + done;
        uint32_t cl_idx = abs_pos / cluster_size;
        uint32_t in_cl = abs_pos % cluster_size;

        uint16_t cl;
        if (fat16_ensure_cluster_for_index(f, cl_idx, &cl) < 0)
            break;

        uint32_t sec_idx = in_cl / FAT16_SECTOR_SIZE;
        uint32_t sec_off = in_cl % FAT16_SECTOR_SIZE;
        uint32_t lba = cluster_to_lba(cl) + sec_idx;

        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(lba, sec) < 0)
            break;

        uint32_t space = FAT16_SECTOR_SIZE - sec_off;
        uint32_t need = len - done;
        uint32_t take = (space < need) ? space : need;

        memcpy(sec + sec_off, (const uint8_t *)buf + done, take);
        if (ata_write_sector(lba, sec) < 0)
            break;

        done += take;
    }

    f->pos += done;
    if (f->pos > f->size) {
        f->size = f->pos;
    }

    if (done > 0) {
        // Update directory entry with new file size. If this fails, the data
        // is on disk but the metadata is stale — log but still report bytes
        // written so the caller knows data reached the disk.
        if (fat16_update_dirent(f) < 0) {
            printf(
                "[fat16] warning: dirent update failed after %d byte write\n",
                (int)done);
        }
    }

    return (int)done;
}

static int fat16_vfs_close(int handle) {
    if (handle < 0 || handle >= FAT16_MAX_OPEN)
        return -1;
    if (!g_open[handle].in_use)
        return -1;
    memset(&g_open[handle], 0, sizeof(g_open[handle]));
    return 0;
}

static int fat16_vfs_seek(int handle, int offset, int whence) {
    if (handle < 0 || handle >= FAT16_MAX_OPEN)
        return -1;
    if (!g_open[handle].in_use)
        return -1;

    fat16_open_t *f = &g_open[handle];
    int pos;
    switch (whence) {
    case SEEK_SET:
        pos = offset;
        break;
    case SEEK_CUR:
        pos = (int)f->pos + offset;
        break;
    case SEEK_END:
        pos = (int)f->size + offset;
        break;
    default:
        return -1;
    }
    if (pos < 0)
        pos = 0;
    if ((uint32_t)pos > f->size)
        pos = (int)f->size;
    f->pos = (uint32_t)pos;
    return pos;
}

static int fat16_vfs_stat(const char *path, vfs_stat_t *st) {
    if (!g_fat.mounted || !path || !st)
        return -1;

    // Check if path is root "/"
    const char *p = path;
    while (*p == '/')
        p++;
    if (*p == '\0') {
        st->size = 0;
        st->type = VFS_DIR;
        return 0;
    }

    fat16_dirent_t de;
    int rc = fat16_resolve_path(path, NULL, &de, NULL, NULL, NULL, NULL, NULL);
    if (rc == -2) {
        st->size = 0;
        st->type = VFS_DIR;
        return 0;
    }
    if (rc < 0)
        return -1;

    st->size = de.file_size;
    st->type = (de.attr & FAT16_ATTR_DIR) ? VFS_DIR : VFS_FILE;
    return 0;
}

// Enumerate entries in a directory. Supports root and subdirectories.
// Skips '.', '..', volume labels, LFN entries, and deleted entries.
static int fat16_vfs_readdir(const char *path, int index, char *buf,
                             uint32_t size) {
    if (!g_fat.mounted || !buf || size == 0 || index < 0)
        return 0;

    fat16_dir_loc_t dir;
    if (fat16_resolve_dir(path, &dir) < 0)
        return 0;

    int vis = 0;
    int is_subdir = (dir.cluster != 0);

    if (dir.cluster == 0) {
        // Root directory — bulk-read sectors
        uint8_t bulk[FAT16_SECTOR_SIZE * 8];
        uint32_t s = 0;
        while (s < g_fat.root_dir_sectors) {
            uint32_t batch = g_fat.root_dir_sectors - s;
            if (batch > 8) batch = 8;
            if (ata_pio_read(g_fat.root_start_lba + s, (uint8_t)batch, bulk) <
                0)
                return 0;
            for (uint32_t b = 0; b < batch; b++) {
                uint8_t *sec = bulk + b * FAT16_SECTOR_SIZE;
                for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
                    fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);
                    if (de->name[0] == 0x00)
                        return 0;
                    if (de->name[0] == 0xE5)
                        continue;
                    if (de->attr == FAT16_ATTR_LFN)
                        continue;
                    if (de->attr & FAT16_ATTR_VOLUMEID)
                        continue;
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
            s += batch;
        }
    } else {
        // Subdirectory: follow cluster chain, bulk-read
        uint8_t cbuf[FAT16_SECTOR_SIZE * 8];
        int can_bulk = (g_fat.sectors_per_cluster <= 8);
        uint16_t cl = dir.cluster;
        while (cl >= 2 && cl < 0xFFF8) {
            uint32_t base_lba = cluster_to_lba(cl);
            uint8_t *data;
            uint8_t sec_single[FAT16_SECTOR_SIZE];

            if (can_bulk) {
                if (ata_pio_read(base_lba, g_fat.sectors_per_cluster, cbuf) < 0)
                    return 0;
                data = cbuf;
            } else {
                data = NULL; // fall through to per-sector below
            }

            for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
                uint8_t *sec;
                if (can_bulk) {
                    sec = data + s * FAT16_SECTOR_SIZE;
                } else {
                    if (ata_read_sector(base_lba + s, sec_single) < 0)
                        return 0;
                    sec = sec_single;
                }
                for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
                    fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);
                    if (de->name[0] == 0x00)
                        return 0;
                    if (de->name[0] == 0xE5)
                        continue;
                    if (de->attr == FAT16_ATTR_LFN)
                        continue;
                    if (de->attr & FAT16_ATTR_VOLUMEID)
                        continue;
                    // Skip '.' and '..' in subdirectory listings
                    if (is_subdir) {
                        if (de->name[0] == '.' && de->name[1] == ' ')
                            continue;
                        if (de->name[0] == '.' && de->name[1] == '.' &&
                            de->name[2] == ' ')
                            continue;
                    }
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
            cl = fat16_get_entry(cl);
        }
    }
    return 0;
}

static int fat16_vfs_unlink(const char *path) {
    if (!g_fat.mounted || !path)
        return -1;

    fat16_dirent_t de;
    uint32_t de_lba = 0;
    uint16_t de_off = 0;
    int rc =
        fat16_resolve_path(path, NULL, &de, &de_lba, &de_off, NULL, NULL, NULL);
    if (rc < 0)
        return -1;
    if (de.attr & FAT16_ATTR_DIR)
        return -1;

    if (de.first_cluster_lo >= 2) {
        if (fat16_free_chain(de.first_cluster_lo) < 0)
            return -1;
    }

    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(de_lba, sec) < 0)
        return -1;
    fat16_dirent_t *wde = (fat16_dirent_t *)(sec + de_off);
    wde->name[0] = 0xE5; // 0xE5 = deleted entry marker
    if (ata_write_sector(de_lba, sec) < 0)
        return -1;

    return 0;
}

// Create a new directory at path. Parent must exist and be a directory.
static int fat16_vfs_mkdir(const char *path) {
    if (!g_fat.mounted || !path)
        return -1;

    fat16_dir_loc_t parent;
    fat16_dirent_t de;
    uint32_t de_lba = 0, free_lba = 0;
    uint16_t de_off = 0, free_off = 0;
    uint8_t name83[11];

    int rc = fat16_resolve_path(path, &parent, &de, &de_lba, &de_off, name83,
                                &free_lba, &free_off);
    if (rc == 0)
        return -1; // already exists
    if (rc == -2)
        return -1; // can't mkdir "/"

    // Need a free slot in the parent directory
    if (free_lba == 0)
        return -1;

    // Allocate a cluster for the new directory's contents
    uint16_t new_cl;
    if (fat16_alloc_cluster(&new_cl) < 0)
        return -1;

    // Write '.' and '..' entries in the new cluster
    uint8_t sec[FAT16_SECTOR_SIZE];
    memset(sec, 0, sizeof(sec));

    // '.' entry — points to self
    fat16_dirent_t *dot = (fat16_dirent_t *)sec;
    memset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = FAT16_ATTR_DIR;
    dot->first_cluster_lo = new_cl;

    // '..' entry — points to parent (0 for root)
    fat16_dirent_t *dotdot = (fat16_dirent_t *)(sec + 32);
    memset(dotdot->name, ' ', 11);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = FAT16_ATTR_DIR;
    dotdot->first_cluster_lo = parent.cluster;

    uint32_t new_lba = cluster_to_lba(new_cl);
    if (ata_write_sector(new_lba, sec) < 0)
        return -1;

    // Write the new directory entry in the parent
    if (ata_read_sector(free_lba, sec) < 0)
        return -1;
    fat16_dirent_t *nde = (fat16_dirent_t *)(sec + free_off);
    memset(nde, 0, sizeof(*nde));
    memcpy(nde->name, name83, 11);
    nde->attr = FAT16_ATTR_DIR;
    nde->first_cluster_lo = new_cl;
    nde->file_size = 0; // directories have size 0 in FAT16
    if (ata_write_sector(free_lba, sec) < 0)
        return -1;

    return 0;
}

// Remove an empty directory. Fails if directory has entries other than . and ..
static int fat16_vfs_rmdir(const char *path) {
    if (!g_fat.mounted || !path)
        return -1;

    fat16_dirent_t de;
    uint32_t de_lba = 0;
    uint16_t de_off = 0;
    int rc =
        fat16_resolve_path(path, NULL, &de, &de_lba, &de_off, NULL, NULL, NULL);
    if (rc < 0)
        return -1;
    if (!(de.attr & FAT16_ATTR_DIR))
        return -1; // not a directory

    // Check that directory is empty (only . and .. entries)
    uint16_t cl = de.first_cluster_lo;
    if (cl < 2)
        return -1;

    uint8_t sec[FAT16_SECTOR_SIZE];
    int has_entries = 0;
    uint16_t check_cl = cl;
    while (check_cl >= 2 && check_cl < 0xFFF8) {
        for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
            uint32_t lba = cluster_to_lba(check_cl) + s;
            if (ata_read_sector(lba, sec) < 0)
                return -1;

            for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
                fat16_dirent_t *entry = (fat16_dirent_t *)(sec + off);
                if (entry->name[0] == 0x00)
                    goto done_check;
                if (entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == FAT16_ATTR_LFN)
                    continue;
                // Skip '.' and '..'
                if (entry->name[0] == '.' && entry->name[1] == ' ')
                    continue;
                if (entry->name[0] == '.' && entry->name[1] == '.' &&
                    entry->name[2] == ' ')
                    continue;
                // Found a real entry — not empty
                has_entries = 1;
                goto done_check;
            }
        }
        check_cl = fat16_get_entry(check_cl);
    }
done_check:
    if (has_entries)
        return -1; // directory not empty

    // Free the directory's cluster chain
    if (fat16_free_chain(cl) < 0)
        return -1;

    // Mark directory entry as deleted in parent
    if (ata_read_sector(de_lba, sec) < 0)
        return -1;
    fat16_dirent_t *wde = (fat16_dirent_t *)(sec + de_off);
    wde->name[0] = 0xE5; // 0xE5 = deleted entry marker
    if (ata_write_sector(de_lba, sec) < 0)
        return -1;

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
    .mkdir = fat16_vfs_mkdir,
    .rmdir = fat16_vfs_rmdir,
};

static int fat16_try_mount_at(uint32_t part_lba) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(part_lba, sec) < 0)
        return -1;

    fat16_bpb_t *b = (fat16_bpb_t *)sec;
    if (b->bytes_per_sector != FAT16_SECTOR_SIZE)
        return -1;
    if (b->sectors_per_cluster == 0)
        return -1;
    if (b->fat_count == 0)
        return -1;
    if (b->root_entry_count == 0)
        return -1;
    if (b->sectors_per_fat_16 == 0)
        return -1;

    uint32_t total =
        b->total_sectors_16 ? b->total_sectors_16 : b->total_sectors_32;
    if (total == 0)
        return -1;

    uint32_t root_secs =
        ((uint32_t)b->root_entry_count * 32u + (FAT16_SECTOR_SIZE - 1)) /
        FAT16_SECTOR_SIZE;
    uint32_t data_secs =
        total - (b->reserved_sector_count +
                 ((uint32_t)b->fat_count * b->sectors_per_fat_16) + root_secs);
    uint32_t clusters = data_secs / b->sectors_per_cluster;

    // FAT16 range
    if (clusters < 4085 || clusters >= 65525)
        return -1;

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
    g_fat.root_start_lba =
        g_fat.fat_start_lba + ((uint32_t)b->fat_count * b->sectors_per_fat_16);
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
    fat_cache_invalidate();

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

const vfs_fs_ops_t *fat16_get_ops(void) { return &fat16_ops; }
