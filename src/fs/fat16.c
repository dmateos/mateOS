#include "fat16.h"
#include "drivers/ata_pio.h"
#include "lib.h"
#include "liballoc/liballoc_1_1.h"
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

// Extra open flags (must match vfs.h / userland/syscalls.h)
#define O_APPEND 0x10

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

// LFN (Long File Name) directory entry — attribute byte 0x0F marks these.
// Entries are stored in reverse order before the short 8.3 dirent, each
// tagged with a 1-based sequence number (last entry ORed with 0x40).
typedef struct __attribute__((packed)) {
    uint8_t  seq;          // sequence number (1-based, 0x40 = last/first-in-chain)
    uint8_t  name1[10];    // 5 UTF-16LE chars
    uint8_t  attr;         // 0x0F = LFN marker
    uint8_t  type;         // 0
    uint8_t  checksum;     // checksum of the associated 8.3 name
    uint8_t  name2[12];    // 6 UTF-16LE chars
    uint16_t cluster;      // 0 (unused for LFN)
    uint8_t  name3[4];     // 2 UTF-16LE chars
} fat16_lfn_t;

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

// ---------------------------------------------------------------------------
// Readdir cache — avoids O(N²) rescanning for index-based readdir API.
// On index==0 (or path change), scan the entire directory once and cache
// all entry names. Subsequent readdir calls just index into the cache.
// ---------------------------------------------------------------------------
#define RDCACHE_MAX 256  // raised from 128: silently truncating directories is confusing
#define RDCACHE_NAME 64  // long filenames up to 63 chars + NUL
static struct {
    char path[FAT16_MAX_PATH]; // cached directory path
    char names[RDCACHE_MAX][RDCACHE_NAME];
    int count;                 // number of cached entries
    int valid;                 // cache is populated
} rdcache;

static void rdcache_invalidate(void) {
    rdcache.valid = 0;
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

// Compute the 8.3 checksum used to associate LFN entries with their short dirent.
static uint8_t lfn_checksum(const uint8_t name83[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name83[i];
    return sum;
}

// Extract up to `count` UTF-16LE chars from `src` into ASCII `out` buffer.
// Stops at NUL or 0xFFFF padding. Returns 0.
static int lfn_extract_chars(const uint8_t *src, int count, char *out, int *pos,
                              int maxlen) {
    for (int i = 0; i < count; i++) {
        uint8_t lo = src[i * 2];
        uint8_t hi = src[i * 2 + 1];
        if (lo == 0xFF && hi == 0xFF) break; // 0xFFFF = padding
        if (lo == 0 && hi == 0) break;       // NUL terminator
        if (*pos < maxlen - 1)
            out[(*pos)++] = (char)lo;        // ASCII-safe: take low byte
    }
    return 0;
}

// Generate a short 8.3 alias for a long name that cannot be represented
// directly. Uses up to 6 chars of base + "~N" suffix, plus up to 3 ext chars.
// Does NOT check for collisions — caller is responsible for uniqueness if needed.
static void fat16_make_short_alias(const char *longname, uint8_t out[11],
                                   int suffix_num) {
    for (int i = 0; i < 11; i++) out[i] = ' ';

    // Find last dot for extension
    const char *dot = (const char *)0;
    for (const char *p = longname; *p; p++) {
        if (*p == '.') dot = p;
    }

    // Build base (up to 6 valid 8.3 chars)
    int base_len = 0;
    const char *p = longname;
    const char *base_end = dot ? dot : (longname + strlen(longname));
    while (p < base_end && base_len < 6) {
        char c = upper_ascii(*p++);
        if (is_83_char(c)) {
            out[base_len++] = (uint8_t)c;
        } else if (c != ' ') {
            out[base_len++] = '_'; // replace invalid char
        }
    }

    // Append ~N suffix (N = suffix_num, 1-9)
    char suffix[3] = { '~', (char)('0' + suffix_num), '\0' };
    // suffix needs 2 chars; if base_len > 6 already capped; just place at [6] and [7]
    // But base is only up to 6, so positions [base_len] and [base_len+1] (if < 8)
    if (base_len > 6) base_len = 6;
    out[base_len]     = '~';
    out[base_len + 1] = (uint8_t)('0' + suffix_num);
    (void)suffix; // silence unused warning

    // Extension: up to 3 chars after the last dot
    if (dot) {
        int ext_len = 0;
        p = dot + 1;
        while (*p && ext_len < 3) {
            char c = upper_ascii(*p++);
            if (is_83_char(c))
                out[8 + ext_len++] = (uint8_t)c;
        }
    }
}

// Write LFN directory entries for `longname` into `sec` at `off`, going
// backwards. `lfn_count` is the number of LFN entries to write (already
// computed). `checksum` is the checksum of the associated 8.3 name.
// Returns 0 on success, -1 if the entries don't fit in the sector
// (caller must ensure there is room).
static void lfn_write_entries(uint8_t *sec, int start_off, int lfn_count,
                               const char *longname, uint8_t checksum) {
    int namelen = (int)strlen(longname);
    // LFN entries are written in reverse order: highest seq first (lowest addr),
    // seq 1 last (just before the 8.3 dirent).
    for (int seq = lfn_count; seq >= 1; seq--) {
        int entry_off = start_off + (seq - 1) * 32;
        fat16_lfn_t *lfn = (fat16_lfn_t *)(sec + entry_off);
        memset(lfn, 0xFF, sizeof(*lfn)); // fill with 0xFF (padding)
        lfn->attr     = FAT16_ATTR_LFN;
        lfn->type     = 0;
        lfn->checksum = checksum;
        lfn->cluster  = 0;
        // which characters does this entry cover?
        // seq 1 covers chars 0-12, seq 2 covers 13-25, etc.
        int char_start = (seq - 1) * 13;
        uint8_t seq_num = (uint8_t)seq;
        if (seq == lfn_count) seq_num |= 0x40; // mark as last (first in reverse order)
        lfn->seq = seq_num;

        // Fill name1[10], name2[12], name3[4] with UTF-16LE chars
        // For each of the 13 chars in this entry:
        // positions 0-4 -> name1, 5-10 -> name2, 11-12 -> name3
        for (int ci = 0; ci < 13; ci++) {
            int char_idx = char_start + ci;
            uint8_t lo, hi;
            if (char_idx < namelen) {
                lo = (uint8_t)longname[char_idx];
                hi = 0;
            } else if (char_idx == namelen) {
                lo = 0; hi = 0; // NUL terminator
            } else {
                lo = 0xFF; hi = 0xFF; // padding
            }
            uint8_t *field;
            int field_idx;
            if (ci < 5) {
                field = lfn->name1;
                field_idx = ci;
            } else if (ci < 11) {
                field = lfn->name2;
                field_idx = ci - 5;
            } else {
                field = lfn->name3;
                field_idx = ci - 11;
            }
            field[field_idx * 2]     = lo;
            field[field_idx * 2 + 1] = hi;
        }
    }
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
// Helper: scan a sector buffer for a matching 8.3 name (or long name) or free slot.
// `pending_lfn` is the long name accumulated from preceding LFN entries (may be "").
// `pending_lfn_start_lba/off` records where those LFN entries began (for free-slot
// reporting when a deleted entry is found).
// Returns 1 if match found, -1 if end-of-directory (0x00 marker), 0 to keep scanning.
static int fat16_scan_dir_sector(const uint8_t *sec, uint32_t lba,
                                 const uint8_t name83[11],
                                 const char *longname,
                                 fat16_dirent_t *out, uint32_t *out_lba,
                                 uint16_t *out_off, uint32_t *free_lba,
                                 uint16_t *free_off,
                                 // LFN state: accumulated name and its storage info
                                 char *pending_lfn, int pending_lfn_maxlen,
                                 int *pending_lfn_seq) {
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
            // Deleted entry: reset pending LFN state and record as free slot
            if (pending_lfn) pending_lfn[0] = '\0';
            if (pending_lfn_seq) *pending_lfn_seq = 0;
            if (free_lba && *free_lba == 0) {
                *free_lba = lba;
                *free_off = (uint16_t)off;
            }
            continue;
        }
        if (de->attr == FAT16_ATTR_LFN) {
            // Accumulate LFN characters into pending_lfn.
            // LFN entries arrive in reverse seq order on disk, so the entry with
            // the highest seq (ORed with 0x40) arrives first, down to seq 1.
            // We process them in arrival order, prepending each entry's chars.
            // Simple approach: rebuild the name when we see each entry.
            // Since we see them from last->first, we can store them and reassemble
            // when we hit the short entry. For simplicity, do a forward pass:
            // treat them in arrival order and just store the whole name.
            if (pending_lfn && pending_lfn_seq) {
                const fat16_lfn_t *lfn = (const fat16_lfn_t *)de;
                uint8_t seq = lfn->seq & 0x3F; // strip 0x40 flag
                if (lfn->seq & 0x40) {
                    // This is the last LFN entry (highest seq, first on disk)
                    // — reset and start accumulating from here
                    *pending_lfn_seq = (int)seq;
                    pending_lfn[0] = '\0';
                }
                // Prepend this entry's chars: entry with seq=N covers chars (N-1)*13..N*13-1
                // Build a temporary buffer for just this entry's contribution
                char chunk[14];
                int cpos = 0;
                lfn_extract_chars(lfn->name1, 5, chunk, &cpos, 14);
                lfn_extract_chars(lfn->name2, 6, chunk, &cpos, 14);
                lfn_extract_chars(lfn->name3, 2, chunk, &cpos, 14);
                chunk[cpos] = '\0';
                // Each chunk corresponds to chars at position (seq-1)*13 in the final name.
                // Rather than complex insertion, just overwrite those positions.
                int char_start = ((int)seq - 1) * 13;
                for (int ci = 0; ci < cpos && (char_start + ci) < pending_lfn_maxlen - 1; ci++)
                    pending_lfn[char_start + ci] = chunk[ci];
                // NUL-terminate at the known end
                int total_chars = *pending_lfn_seq * 13;
                if (total_chars < pending_lfn_maxlen)
                    pending_lfn[total_chars] = '\0';
                else
                    pending_lfn[pending_lfn_maxlen - 1] = '\0';
            }
            continue;
        }
        if (de->attr & FAT16_ATTR_VOLUMEID) {
            // Volume label: reset pending LFN
            if (pending_lfn) pending_lfn[0] = '\0';
            if (pending_lfn_seq) *pending_lfn_seq = 0;
            continue;
        }

        // Short 8.3 entry: check for match
        int matched = 0;
        if (memcmp(de->name, name83, 11) == 0)
            matched = 1;
        // Also match by long name if provided and pending_lfn has content
        if (!matched && longname && pending_lfn && pending_lfn[0] != '\0') {
            if (strcmp(pending_lfn, longname) == 0)
                matched = 1;
        }

        if (matched) {
            if (out)   *out    = *de;
            if (out_lba) *out_lba = lba;
            if (out_off) *out_off = (uint16_t)off;
            // Reset pending LFN state
            if (pending_lfn) pending_lfn[0] = '\0';
            if (pending_lfn_seq) *pending_lfn_seq = 0;
            return 1; // found
        }

        // No match: reset pending LFN for next entry
        if (pending_lfn) pending_lfn[0] = '\0';
        if (pending_lfn_seq) *pending_lfn_seq = 0;
    }
    return 0; // keep scanning
}

// Lookup a name in a directory. Supports both 8.3 and long file names.
// `name83` is the 8.3-encoded name (11 bytes); `longname` is the original
// filename string (may be NULL). Matches on either 8.3 or LFN.
static int fat16_lookup_in_dir(fat16_dir_loc_t dir, const uint8_t name83[11],
                               const char *longname,
                               fat16_dirent_t *out, uint32_t *out_lba,
                               uint16_t *out_off, uint32_t *free_lba,
                               uint16_t *free_off) {
    if (!g_fat.mounted || !name83)
        return -1;

    // Heap-allocate bulk read buffer to avoid kernel stack overflow (8KB limit)
    uint32_t bufsz = FAT16_SECTOR_SIZE * 8;
    uint8_t *bulk = (uint8_t *)kmalloc(bufsz);
    if (!bulk)
        return -1;
    int result = -1;

    // LFN accumulator state (persists across sector boundaries)
    char pending_lfn[RDCACHE_NAME];
    int  pending_lfn_seq = 0;
    pending_lfn[0] = '\0';

    if (dir.cluster == 0) {
        // Root directory: fixed LBA region — bulk-read up to 8 sectors
        uint32_t s = 0;
        while (s < g_fat.root_dir_sectors) {
            uint32_t batch = g_fat.root_dir_sectors - s;
            if (batch > 8) batch = 8;
            uint32_t base_lba = g_fat.root_start_lba + s;
            if (ata_pio_read(base_lba, (uint8_t)batch, bulk) < 0)
                goto out;
            for (uint32_t b = 0; b < batch; b++) {
                int rc = fat16_scan_dir_sector(
                    bulk + b * FAT16_SECTOR_SIZE, base_lba + b,
                    name83, longname, out, out_lba, out_off, free_lba, free_off,
                    pending_lfn, RDCACHE_NAME, &pending_lfn_seq);
                if (rc == 1) { result = 0; goto out; }
                if (rc == -1) goto out;
            }
            s += batch;
        }
    } else {
        // Subdirectory: follow cluster chain, bulk-read entire cluster
        int can_bulk = (g_fat.sectors_per_cluster <= 8);
        uint16_t cl = dir.cluster;
        while (cl >= 2 && cl < 0xFFF8) {
            uint32_t base_lba = cluster_to_lba(cl);
            if (can_bulk) {
                if (ata_pio_read(base_lba, g_fat.sectors_per_cluster, bulk) < 0)
                    goto out;
                for (uint8_t b = 0; b < g_fat.sectors_per_cluster; b++) {
                    int rc = fat16_scan_dir_sector(
                        bulk + b * FAT16_SECTOR_SIZE, base_lba + b,
                        name83, longname, out, out_lba, out_off, free_lba, free_off,
                        pending_lfn, RDCACHE_NAME, &pending_lfn_seq);
                    if (rc == 1) { result = 0; goto out; }
                    if (rc == -1) goto out;
                }
            } else {
                uint8_t sec[FAT16_SECTOR_SIZE];
                for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
                    if (ata_read_sector(base_lba + s, sec) < 0)
                        goto out;
                    int rc = fat16_scan_dir_sector(
                        sec, base_lba + s,
                        name83, longname, out, out_lba, out_off, free_lba, free_off,
                        pending_lfn, RDCACHE_NAME, &pending_lfn_seq);
                    if (rc == 1) { result = 0; goto out; }
                    if (rc == -1) goto out;
                }
            }
            cl = fat16_get_entry(cl);
        }
    }

out:
    kfree(bulk);
    return result;
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

        // Extract component name (up to RDCACHE_NAME-1 chars for LFN support)
        char comp[RDCACHE_NAME];
        if (comp_len >= (int)sizeof(comp))
            return -1; // name too long
        memcpy(comp, path, (size_t)comp_len);
        comp[comp_len] = '\0';

        // Convert to 8.3 — may fail for long names; use all-spaces as sentinel
        uint8_t c83[11];
        int c83_ok = (fat16_name_to_83(comp, c83) == 0);
        if (!c83_ok) {
            // Long name: build a dummy 8.3 alias for the lookup.
            // The scan will match via the LFN string comparison path.
            for (int i = 0; i < 11; i++) c83[i] = 0;
        }

        // Is this the last component?
        const char *rest = end;
        while (*rest == '/')
            rest++;
        int is_last = (*rest == '\0');

        if (is_last) {
            // This is the final component — look it up in cur_dir
            if (parent_dir)
                *parent_dir = cur_dir;
            if (name83) {
                if (c83_ok)
                    memcpy(name83, c83, 11);
                else
                    memset(name83, ' ', 11); // signal: long name, alias unknown until found
            }
            if (free_lba)
                *free_lba = 0;
            if (free_off)
                *free_off = 0;
            return fat16_lookup_in_dir(cur_dir, c83, comp, final_de, de_lba, de_off,
                                       free_lba, free_off);
        } else {
            // Intermediate component — must be a directory
            fat16_dirent_t de;
            if (fat16_lookup_in_dir(cur_dir, c83, comp, &de, NULL, NULL, NULL, NULL) <
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
    // Heap-allocate cluster buffer to avoid kernel stack overflow (8KB limit)
    int can_bulk = (g_fat.sectors_per_cluster <= 8);
    uint8_t *cluster_buf = NULL;
    if (can_bulk) {
        cluster_buf = (uint8_t *)kmalloc(FAT16_SECTOR_SIZE * 8);
        if (!cluster_buf)
            can_bulk = 0; // fall back to per-sector reads
    }

    while (done < len && cl >= 2 && cl < 0xFFF8) {
        uint32_t lba = cluster_to_lba(cl);

        if (can_bulk && in_cluster == 0 && (len - done) >= cluster_size) {
            // Fast path: read entire cluster directly into output buffer
            if (ata_pio_read(lba, g_fat.sectors_per_cluster, out + done) < 0)
                break;
            done += cluster_size;
        } else if (can_bulk) {
            // Partial cluster: bulk read into temp buffer, copy needed portion
            if (ata_pio_read(lba, g_fat.sectors_per_cluster, cluster_buf) < 0)
                break;
            uint32_t avail = cluster_size - in_cluster;
            uint32_t need = len - done;
            uint32_t take = (avail < need) ? avail : need;
            memcpy(out + done, cluster_buf + in_cluster, take);
            done += take;
        } else {
            // Large cluster or OOM: read sector by sector
            for (uint8_t s = 0; s < g_fat.sectors_per_cluster && done < len;
                 s++) {
                uint32_t sector_off = (uint32_t)s * FAT16_SECTOR_SIZE;
                if (in_cluster >= sector_off + FAT16_SECTOR_SIZE)
                    continue;

                uint8_t sec[FAT16_SECTOR_SIZE];
                if (ata_read_sector(lba + s, sec) < 0) {
                    if (cluster_buf) kfree(cluster_buf);
                    return (int)done;
                }

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

    if (cluster_buf) kfree(cluster_buf);
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
        rdcache_invalidate(); // new file being created

        // Determine whether we need LFN entries.
        // Extract the last path component as the filename.
        const char *fname = path;
        for (const char *p = path; *p; p++)
            if (*p == '/') fname = p + 1;
        int need_lfn = (fat16_name_to_83(fname, name83) < 0);
        uint8_t actual_name83[11];
        if (need_lfn) {
            // Generate a short alias and write LFN entries first.
            // Find a non-colliding alias suffix.
            int suffix = 1;
            fat16_make_short_alias(fname, actual_name83, suffix);
            // Simple collision check: if alias already exists, try next suffix
            while (suffix <= 9) {
                fat16_dirent_t check_de;
                fat16_make_short_alias(fname, actual_name83, suffix);
                if (fat16_lookup_in_dir(parent, actual_name83, NULL,
                                        &check_de, NULL, NULL, NULL, NULL) < 0)
                    break; // not found = no collision
                suffix++;
            }
            if (suffix > 9) return -1; // gave up

            // Compute how many LFN entries we need
            int fname_len = (int)strlen(fname);
            int lfn_count = (fname_len + 12) / 13; // ceil(len/13)
            uint8_t checksum = lfn_checksum(actual_name83);

            // We need lfn_count + 1 consecutive free slots. For now, require
            // the free slot found earlier to be followed by enough space in
            // the same sector. This is a simplified implementation.
            // If there's no room in the sector, just write the short entry only.
            int can_fit_lfn = (free_off + (uint16_t)((lfn_count + 1) * 32) <= FAT16_SECTOR_SIZE);

            uint8_t sec[FAT16_SECTOR_SIZE];
            if (ata_read_sector(free_lba, sec) < 0)
                return -1;

            if (can_fit_lfn) {
                // Write LFN entries before the short entry
                lfn_write_entries(sec, (int)free_off, lfn_count, fname, checksum);
                // Write short entry after LFN entries
                int short_off = (int)free_off + lfn_count * 32;
                fat16_dirent_t *nde = (fat16_dirent_t *)(sec + short_off);
                memset(nde, 0, sizeof(*nde));
                memcpy(nde->name, actual_name83, 11);
                nde->attr = FAT16_ATTR_ARCHIVE;
                if (ata_write_sector(free_lba, sec) < 0)
                    return -1;
                de = *nde;
                de_lba = free_lba;
                de_off = (uint16_t)short_off;
            } else {
                // Not enough room in this sector: just write short name only
                fat16_dirent_t *nde = (fat16_dirent_t *)(sec + free_off);
                memset(nde, 0, sizeof(*nde));
                memcpy(nde->name, actual_name83, 11);
                nde->attr = FAT16_ATTR_ARCHIVE;
                if (ata_write_sector(free_lba, sec) < 0)
                    return -1;
                de = *nde;
                de_lba = free_lba;
                de_off = free_off;
            }
            memcpy(name83, actual_name83, 11);
        } else {
            // Normal 8.3 name
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

    // O_APPEND: always write at end of file
    if (f->flags & O_APPEND)
        f->pos = f->size;

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

// Scan a directory and collect all visible entry names into the rdcache.
// Called once per directory; subsequent readdir calls index into the cache.
static void fat16_readdir_fill_cache(const char *path, fat16_dir_loc_t dir) {
    rdcache.count = 0;
    rdcache.valid = 0;

    // Store the path for cache-hit comparison
    int pi = 0;
    if (path) {
        for (; path[pi] && pi < FAT16_MAX_PATH - 1; pi++)
            rdcache.path[pi] = path[pi];
    }
    rdcache.path[pi] = '\0';

    int is_subdir = (dir.cluster != 0);

    // Heap-allocate bulk read buffer to avoid kernel stack overflow (8KB limit)
    uint8_t *bulk = (uint8_t *)kmalloc(FAT16_SECTOR_SIZE * 8);
    if (!bulk)
        return;

    // LFN accumulator: collect pending long-name from LFN entries
    char pending_lfn[RDCACHE_NAME];
    int  pending_lfn_seq = 0;
    pending_lfn[0] = '\0';

    // Helper macro to add an entry to the cache, using LFN if available
#define RDCACHE_ADD(de_ptr) do { \
    if (rdcache.count < RDCACHE_MAX) { \
        if (pending_lfn[0] != '\0') { \
            int _n = 0; \
            for (; pending_lfn[_n] && _n < RDCACHE_NAME - 1; _n++) \
                rdcache.names[rdcache.count][_n] = pending_lfn[_n]; \
            rdcache.names[rdcache.count][_n] = '\0'; \
        } else { \
            fat16_dirent_name_to_string((de_ptr), rdcache.names[rdcache.count]); \
        } \
        rdcache.count++; \
    } \
    pending_lfn[0] = '\0'; \
    pending_lfn_seq = 0; \
} while (0)

    if (dir.cluster == 0) {
        // Root directory — bulk-read sectors
        uint32_t s = 0;
        while (s < g_fat.root_dir_sectors && rdcache.count < RDCACHE_MAX) {
            uint32_t batch = g_fat.root_dir_sectors - s;
            if (batch > 8) batch = 8;
            if (ata_pio_read(g_fat.root_start_lba + s, (uint8_t)batch, bulk) <
                0)
                goto done;
            for (uint32_t b = 0; b < batch; b++) {
                uint8_t *sec = bulk + b * FAT16_SECTOR_SIZE;
                for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
                    fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);
                    if (de->name[0] == 0x00)
                        goto done;
                    if (de->name[0] == 0xE5) {
                        pending_lfn[0] = '\0'; pending_lfn_seq = 0;
                        continue;
                    }
                    if (de->attr == FAT16_ATTR_LFN) {
                        // Accumulate LFN chars
                        const fat16_lfn_t *lfn = (const fat16_lfn_t *)de;
                        uint8_t seq = lfn->seq & 0x3F;
                        if (lfn->seq & 0x40) {
                            pending_lfn_seq = (int)seq;
                            pending_lfn[0] = '\0';
                        }
                        int char_start = ((int)seq - 1) * 13;
                        char chunk[14]; int cpos = 0;
                        lfn_extract_chars(lfn->name1, 5, chunk, &cpos, 14);
                        lfn_extract_chars(lfn->name2, 6, chunk, &cpos, 14);
                        lfn_extract_chars(lfn->name3, 2, chunk, &cpos, 14);
                        for (int ci = 0; ci < cpos && (char_start+ci) < RDCACHE_NAME-1; ci++)
                            pending_lfn[char_start + ci] = chunk[ci];
                        int total_chars = pending_lfn_seq * 13;
                        if (total_chars < RDCACHE_NAME)
                            pending_lfn[total_chars] = '\0';
                        else
                            pending_lfn[RDCACHE_NAME - 1] = '\0';
                        continue;
                    }
                    if (de->attr & FAT16_ATTR_VOLUMEID) {
                        pending_lfn[0] = '\0'; pending_lfn_seq = 0;
                        continue;
                    }
                    RDCACHE_ADD(de);
                }
            }
            s += batch;
        }
    } else {
        // Subdirectory: follow cluster chain, bulk-read
        int can_bulk = (g_fat.sectors_per_cluster <= 8);
        uint16_t cl = dir.cluster;
        while (cl >= 2 && cl < 0xFFF8 && rdcache.count < RDCACHE_MAX) {
            uint32_t base_lba = cluster_to_lba(cl);
            uint8_t *data;
            uint8_t sec_single[FAT16_SECTOR_SIZE];

            if (can_bulk) {
                if (ata_pio_read(base_lba, g_fat.sectors_per_cluster, bulk) < 0)
                    goto done;
                data = bulk;
            } else {
                data = NULL;
            }

            for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
                uint8_t *sec;
                if (can_bulk) {
                    sec = data + s * FAT16_SECTOR_SIZE;
                } else {
                    if (ata_read_sector(base_lba + s, sec_single) < 0)
                        goto done;
                    sec = sec_single;
                }
                for (int off = 0; off < FAT16_SECTOR_SIZE; off += 32) {
                    fat16_dirent_t *de = (fat16_dirent_t *)(sec + off);
                    if (de->name[0] == 0x00)
                        goto done;
                    if (de->name[0] == 0xE5) {
                        pending_lfn[0] = '\0'; pending_lfn_seq = 0;
                        continue;
                    }
                    if (de->attr == FAT16_ATTR_LFN) {
                        const fat16_lfn_t *lfn = (const fat16_lfn_t *)de;
                        uint8_t seq = lfn->seq & 0x3F;
                        if (lfn->seq & 0x40) {
                            pending_lfn_seq = (int)seq;
                            pending_lfn[0] = '\0';
                        }
                        int char_start = ((int)seq - 1) * 13;
                        char chunk[14]; int cpos = 0;
                        lfn_extract_chars(lfn->name1, 5, chunk, &cpos, 14);
                        lfn_extract_chars(lfn->name2, 6, chunk, &cpos, 14);
                        lfn_extract_chars(lfn->name3, 2, chunk, &cpos, 14);
                        for (int ci = 0; ci < cpos && (char_start+ci) < RDCACHE_NAME-1; ci++)
                            pending_lfn[char_start + ci] = chunk[ci];
                        int total_chars = pending_lfn_seq * 13;
                        if (total_chars < RDCACHE_NAME)
                            pending_lfn[total_chars] = '\0';
                        else
                            pending_lfn[RDCACHE_NAME - 1] = '\0';
                        continue;
                    }
                    if (de->attr & FAT16_ATTR_VOLUMEID) {
                        pending_lfn[0] = '\0'; pending_lfn_seq = 0;
                        continue;
                    }
                    if (is_subdir) {
                        if (de->name[0] == '.' && de->name[1] == ' ') {
                            pending_lfn[0] = '\0'; continue;
                        }
                        if (de->name[0] == '.' && de->name[1] == '.' &&
                            de->name[2] == ' ') {
                            pending_lfn[0] = '\0'; continue;
                        }
                    }
                    RDCACHE_ADD(de);
                }
            }
            cl = fat16_get_entry(cl);
        }
    }
#undef RDCACHE_ADD

done:
    kfree(bulk);
    rdcache.valid = 1;
}

// Check if the readdir cache is valid for the given path.
static int rdcache_hit(const char *path) {
    if (!rdcache.valid)
        return 0;
    const char *a = path ? path : "";
    return (strcmp(a, rdcache.path) == 0);
}

// Enumerate entries in a directory. Supports root and subdirectories.
// Skips '.', '..', volume labels, LFN entries, and deleted entries.
// Uses a cache: the first call (or path change) scans once; subsequent
// calls return from cache in O(1).
static int fat16_vfs_readdir(const char *path, int index, char *buf,
                             uint32_t size) {
    if (!g_fat.mounted || !buf || size == 0 || index < 0)
        return 0;

    // Populate cache on first call or path change
    if (!rdcache_hit(path)) {
        fat16_dir_loc_t dir;
        if (fat16_resolve_dir(path, &dir) < 0)
            return 0;
        fat16_readdir_fill_cache(path, dir);
    }

    if (index >= rdcache.count)
        return 0;

    const char *name = rdcache.names[index];
    size_t n = strlen(name);
    if (n >= size) n = size - 1;
    memcpy(buf, name, n);
    buf[n] = '\0';
    return (int)(n + 1);
}

static int fat16_vfs_unlink(const char *path) {
    if (!g_fat.mounted || !path)
        return -1;
    rdcache_invalidate(); // directory contents will change

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
    rdcache_invalidate(); // directory contents will change

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
    rdcache_invalidate(); // directory contents will change

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

// ---------------------------------------------------------------------------
// Rename: move/rename a file or directory.
// ---------------------------------------------------------------------------
// Helper: mark LFN entries preceding a dirent as deleted.
// Scans backward from `de_off` in the sector at `de_lba`, marking 0xE5.
static void fat16_delete_preceding_lfn(uint32_t de_lba, uint16_t de_off) {
    uint8_t sec[FAT16_SECTOR_SIZE];
    if (ata_read_sector(de_lba, sec) < 0)
        return;
    int off = (int)de_off - 32;
    int changed = 0;
    while (off >= 0) {
        fat16_dirent_t *e = (fat16_dirent_t *)(sec + off);
        if (e->attr == FAT16_ATTR_LFN && e->name[0] != 0xE5) {
            e->name[0] = 0xE5; // mark deleted
            changed = 1;
            off -= 32;
        } else {
            break;
        }
    }
    if (changed)
        ata_write_sector(de_lba, sec);
}

static int fat16_vfs_rename(const char *oldpath, const char *newpath) {
    if (!g_fat.mounted || !oldpath || !newpath)
        return -1;
    rdcache_invalidate();

    // Resolve old path
    fat16_dir_loc_t old_parent;
    fat16_dirent_t old_de;
    uint32_t old_lba = 0;
    uint16_t old_off = 0;
    uint8_t old_name83[11];
    int rc = fat16_resolve_path(oldpath, &old_parent, &old_de, &old_lba,
                                &old_off, old_name83, NULL, NULL);
    if (rc < 0)
        return -1; // old path doesn't exist

    // Resolve new path (parent must exist)
    fat16_dir_loc_t new_parent;
    fat16_dirent_t new_de;
    uint32_t new_lba = 0, new_free_lba = 0;
    uint16_t new_off = 0, new_free_off = 0;
    uint8_t new_name83[11];
    int new_rc = fat16_resolve_path(newpath, &new_parent, &new_de, &new_lba,
                                    &new_off, new_name83, &new_free_lba,
                                    &new_free_off);

    // Extract new filename component
    const char *new_fname = newpath;
    for (const char *p = newpath; *p; p++)
        if (*p == '/') new_fname = p + 1;

    if (new_rc == 0) {
        // Target already exists
        if (new_de.attr & FAT16_ATTR_DIR)
            return -1; // can't overwrite directory
        // Delete the existing target file first
        if (new_de.first_cluster_lo >= 2)
            fat16_free_chain(new_de.first_cluster_lo);
        {
            uint8_t sec[FAT16_SECTOR_SIZE];
            if (ata_read_sector(new_lba, sec) < 0) return -1;
            fat16_dirent_t *wde = (fat16_dirent_t *)(sec + new_off);
            wde->name[0] = 0xE5;
            if (ata_write_sector(new_lba, sec) < 0) return -1;
            fat16_delete_preceding_lfn(new_lba, new_off);
        }
        // Use the freed slot as our free slot
        new_free_lba = new_lba;
        new_free_off = new_off;
    }

    // Determine if same parent (compare cluster values)
    int same_parent = (old_parent.cluster == new_parent.cluster);

    // Build new 8.3 name (may need LFN)
    int new_need_lfn = (fat16_name_to_83(new_fname, new_name83) < 0);
    uint8_t actual_new83[11];
    if (new_need_lfn) {
        int suffix = 1;
        while (suffix <= 9) {
            fat16_dirent_t check_de;
            fat16_make_short_alias(new_fname, actual_new83, suffix);
            if (fat16_lookup_in_dir(new_parent, actual_new83, NULL,
                                    &check_de, NULL, NULL, NULL, NULL) < 0)
                break;
            suffix++;
        }
        if (suffix > 9) return -1;
    } else {
        memcpy(actual_new83, new_name83, 11);
    }

    if (same_parent && !new_need_lfn) {
        // Simple in-place rename: just update the name83 in the existing dirent.
        // Delete old LFN entries (in same sector, before old dirent).
        fat16_delete_preceding_lfn(old_lba, old_off);
        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(old_lba, sec) < 0) return -1;
        fat16_dirent_t *wde = (fat16_dirent_t *)(sec + old_off);
        memcpy(wde->name, actual_new83, 11);
        return ata_write_sector(old_lba, sec);
    }

    // For cross-directory or LFN rename: write new dirent, mark old as deleted.
    if (new_free_lba == 0)
        return -1; // no free slot in new parent

    // Write new dirent (with LFN if needed)
    {
        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(new_free_lba, sec) < 0) return -1;

        if (new_need_lfn) {
            int fname_len = (int)strlen(new_fname);
            int lfn_count = (fname_len + 12) / 13;
            uint8_t checksum = lfn_checksum(actual_new83);
            int can_fit = (new_free_off + (uint16_t)((lfn_count + 1) * 32) <= FAT16_SECTOR_SIZE);
            if (can_fit) {
                lfn_write_entries(sec, (int)new_free_off, lfn_count, new_fname, checksum);
                int short_off = (int)new_free_off + lfn_count * 32;
                fat16_dirent_t *nde = (fat16_dirent_t *)(sec + short_off);
                *nde = old_de;
                memcpy(nde->name, actual_new83, 11);
            } else {
                fat16_dirent_t *nde = (fat16_dirent_t *)(sec + new_free_off);
                *nde = old_de;
                memcpy(nde->name, actual_new83, 11);
            }
        } else {
            fat16_dirent_t *nde = (fat16_dirent_t *)(sec + new_free_off);
            *nde = old_de;
            memcpy(nde->name, actual_new83, 11);
        }

        if (ata_write_sector(new_free_lba, sec) < 0) return -1;
    }

    // Mark old dirent as deleted, and delete its preceding LFN entries
    fat16_delete_preceding_lfn(old_lba, old_off);
    {
        uint8_t sec[FAT16_SECTOR_SIZE];
        if (ata_read_sector(old_lba, sec) < 0) return -1;
        fat16_dirent_t *wde = (fat16_dirent_t *)(sec + old_off);
        wde->name[0] = 0xE5;
        if (ata_write_sector(old_lba, sec) < 0) return -1;
    }

    // Update any open file handles that reference the old dirent location
    // so that fat16_update_dirent() still works correctly after rename.
    for (int i = 0; i < FAT16_MAX_OPEN; i++) {
        if (g_open[i].in_use && g_open[i].dirent_lba == old_lba &&
            g_open[i].dirent_off == old_off) {
            g_open[i].dirent_lba = new_free_lba;
            g_open[i].dirent_off = new_free_off;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// ftruncate: resize an open file to exactly `length` bytes.
// ---------------------------------------------------------------------------
static int fat16_vfs_ftruncate(int handle, uint32_t length) {
    if (handle < 0 || handle >= FAT16_MAX_OPEN)
        return -1;
    if (!g_open[handle].in_use)
        return -1;

    fat16_open_t *f = &g_open[handle];

    if (length == f->size)
        return 0; // no-op

    uint32_t cluster_size =
        (uint32_t)g_fat.sectors_per_cluster * FAT16_SECTOR_SIZE;

    if (length < f->size) {
        // Shrink: free excess clusters beyond `length`.
        if (length == 0) {
            // Free entire chain
            if (f->first_cluster >= 2) {
                if (fat16_free_chain(f->first_cluster) < 0)
                    return -1;
                f->first_cluster = 0;
            }
        } else {
            // Walk to the cluster containing (length - 1), set its FAT entry
            // to EOC, free the rest.
            uint32_t keep_idx = (length - 1) / cluster_size;
            uint16_t cl = f->first_cluster;
            for (uint32_t i = 0; i < keep_idx; i++) {
                uint16_t next = fat16_get_entry(cl);
                if (next >= 0xFFF8 || next < 2)
                    break;
                cl = next;
            }
            // Free everything after cl
            uint16_t next = fat16_get_entry(cl);
            if (fat16_set_entry(cl, FAT16_EOC) < 0)
                return -1;
            if (next >= 2 && next < 0xFFF8)
                fat16_free_chain(next);
        }

        f->size = length;
        if (f->pos > length)
            f->pos = length;
    } else {
        // Grow: allocate clusters until we reach `length`.
        // New clusters from fat16_alloc_cluster are zeroed.  Bytes within
        // the existing last cluster beyond old_size are zeroed here via
        // read-modify-write on the sector(s) spanning [old_size, cluster_end).
        uint32_t old_size = f->size;
        uint32_t need_clusters = (length + cluster_size - 1) / cluster_size;
        if (need_clusters == 0) need_clusters = 1;
        uint16_t cl;
        if (fat16_ensure_cluster_for_index(f, need_clusters - 1, &cl) < 0)
            return -1;
        f->size = length;

        // Zero stale bytes in the old last cluster (from old_size to the
        // lesser of the cluster boundary and the new length).
        if (old_size > 0 && old_size < length) {
            uint32_t old_cl_idx = (old_size - 1) / cluster_size;
            uint32_t cl_end = (old_cl_idx + 1) * cluster_size;
            uint32_t zero_end = cl_end < length ? cl_end : length;
            if (old_size < zero_end) {
                // Walk FAT chain to old last cluster
                uint16_t zcl = f->first_cluster;
                for (uint32_t i = 0; i < old_cl_idx; i++) {
                    uint16_t next = fat16_get_entry(zcl);
                    if (next >= 0xFFF8 || next < 2) break;
                    zcl = next;
                }
                uint32_t lba = cluster_to_lba(zcl);
                // Iterate over affected sectors within the cluster
                uint32_t pos = old_size;
                while (pos < zero_end) {
                    uint32_t sec = pos / FAT16_SECTOR_SIZE;
                    uint32_t boff = pos % FAT16_SECTOR_SIZE;
                    uint32_t bend = FAT16_SECTOR_SIZE;
                    uint32_t sec_end = (sec + 1) * FAT16_SECTOR_SIZE;
                    if (sec_end > zero_end) bend = zero_end % FAT16_SECTOR_SIZE;
                    if (bend == 0) bend = FAT16_SECTOR_SIZE;
                    uint8_t sec_buf[FAT16_SECTOR_SIZE];
                    if (ata_read_sector(lba + sec, sec_buf) == 0) {
                        for (uint32_t b = boff; b < bend; b++) sec_buf[b] = 0;
                        ata_write_sector(lba + sec, sec_buf);
                    }
                    pos = sec_end;
                }
            }
        }
    }

    // Update directory entry on disk
    if (fat16_update_dirent(f) < 0)
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
    .rename = fat16_vfs_rename,
    .ftruncate = fat16_vfs_ftruncate,
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
