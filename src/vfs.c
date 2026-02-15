#include "vfs.h"
#include "liballoc/liballoc_1_1.h"
#include "pmm.h"
#include "liballoc/liballoc_hooks.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/util.h"
#include "arch/i686/pci.h"

static const vfs_fs_ops_t *filesystems[VFS_MAX_FILESYSTEMS];
static int fs_count = 0;

#define VFS_MAX_VIRTUAL_FILES 8
#define VFS_VIRT_BASE_ID (-1000)

typedef struct {
    const char *name;
    uint32_t (*size_fn)(void);
    int (*read_fn)(uint32_t offset, void *buf, uint32_t len);
} vfs_virtual_file_t;

static vfs_virtual_file_t virtual_files[VFS_MAX_VIRTUAL_FILES];
static int virtual_file_count = 0;

static char vgen_buf[4096];

typedef uint32_t (*vgen_fn_t)(char *dst, uint32_t cap);

static int append_char(char *dst, uint32_t cap, uint32_t *len, char c) {
    if (*len >= cap) return -1;
    dst[*len] = c;
    (*len)++;
    return 0;
}

static int append_cstr(char *dst, uint32_t cap, uint32_t *len, const char *s) {
    while (*s) {
        if (append_char(dst, cap, len, *s++) < 0) return -1;
    }
    return 0;
}

static int append_dec_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v) {
    if (v == 0) return append_char(dst, cap, len, '0');
    char tmp[16];
    int t = 0;
    while (v > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = t - 1; i >= 0; i--) {
        if (append_char(dst, cap, len, tmp[i]) < 0) return -1;
    }
    return 0;
}

static int append_hex_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v) {
    if (append_cstr(dst, cap, len, "0x") < 0) return -1;
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nib = (v >> (uint32_t)shift) & 0xF;
        if (!started && nib == 0 && shift > 0) continue;
        started = 1;
        char c = (char)(nib < 10 ? ('0' + nib) : ('a' + (nib - 10)));
        if (append_char(dst, cap, len, c) < 0) return -1;
    }
    if (!started) {
        if (append_char(dst, cap, len, '0') < 0) return -1;
    }
    return 0;
}

static uint32_t vgen_meminfo(char *dst, uint32_t cap) {
    uint32_t len = 0;
    uint32_t total = 0, used = 0, free_frames = 0;
    uint32_t hstart = 0, hend = 0, hcur = 0;

    pmm_get_stats(&total, &used, &free_frames);
    liballoc_heap_info(&hstart, &hend, &hcur);

    uint32_t htotal = hend - hstart;
    uint32_t hused = (hcur > hstart) ? (hcur - hstart) : 0;
    uint32_t hfree = (htotal > hused) ? (htotal - hused) : 0;

    append_cstr(dst, cap, &len, "PMM: total=");
    append_dec_u32(dst, cap, &len, total);
    append_cstr(dst, cap, &len, " used=");
    append_dec_u32(dst, cap, &len, used);
    append_cstr(dst, cap, &len, " free=");
    append_dec_u32(dst, cap, &len, free_frames);
    append_cstr(dst, cap, &len, " frames (4KB each)\n");

    append_cstr(dst, cap, &len, "Heap: start=");
    append_hex_u32(dst, cap, &len, hstart);
    append_cstr(dst, cap, &len, " end=");
    append_hex_u32(dst, cap, &len, hend);
    append_cstr(dst, cap, &len, " cur=");
    append_hex_u32(dst, cap, &len, hcur);
    append_cstr(dst, cap, &len, "\nHeap: used=");
    append_dec_u32(dst, cap, &len, hused);
    append_cstr(dst, cap, &len, " bytes free=");
    append_dec_u32(dst, cap, &len, hfree);
    append_cstr(dst, cap, &len, " bytes total=");
    append_dec_u32(dst, cap, &len, htotal);
    append_cstr(dst, cap, &len, " bytes\n");

    return len;
}

static uint32_t vgen_cpuinfo(char *dst, uint32_t cap) {
    uint32_t len = 0;
    cpu_info_t info;
    cpu_get_info(&info);

    append_cstr(dst, cap, &len, "CPU vendor: ");
    append_cstr(dst, cap, &len, info.vendor);
    append_cstr(dst, cap, &len, "\nCPUID max leaf: ");
    append_hex_u32(dst, cap, &len, info.max_leaf);
    append_cstr(dst, cap, &len, "\nFamily: ");
    append_dec_u32(dst, cap, &len, info.family);
    append_cstr(dst, cap, &len, "  Model: ");
    append_dec_u32(dst, cap, &len, info.model);
    append_cstr(dst, cap, &len, "  Stepping: ");
    append_dec_u32(dst, cap, &len, info.stepping);
    append_cstr(dst, cap, &len, "\nFeature ECX: ");
    append_hex_u32(dst, cap, &len, info.feature_ecx);
    append_cstr(dst, cap, &len, "\nFeature EDX: ");
    append_hex_u32(dst, cap, &len, info.feature_edx);
    append_cstr(dst, cap, &len, "\n");

    return len;
}

static uint32_t vgen_lsirq(char *dst, uint32_t cap) {
    uint32_t len = 0;
    irq_info_t irq[16];
    int count = irq_get_snapshot(irq, 16);

    append_cstr(dst, cap, &len, "IRQ  Vec  Masked  Handler  Addr        Name\n");
    for (int i = 0; i < count; i++) {
        append_dec_u32(dst, cap, &len, irq[i].irq);
        append_cstr(dst, cap, &len, "    ");
        append_hex_u32(dst, cap, &len, irq[i].vec);
        append_cstr(dst, cap, &len, "   ");
        append_cstr(dst, cap, &len, irq[i].masked ? "yes" : "no");
        append_cstr(dst, cap, &len, "      ");
        append_cstr(dst, cap, &len, irq[i].has_handler ? "yes" : "no");
        append_cstr(dst, cap, &len, "      ");
        if (irq[i].handler_addr) {
            append_hex_u32(dst, cap, &len, irq[i].handler_addr);
        } else {
            append_cstr(dst, cap, &len, "-");
        }
        append_cstr(dst, cap, &len, "    ");
        if (irq[i].handler_name && irq[i].handler_name[0]) {
            append_cstr(dst, cap, &len, irq[i].handler_name);
        } else {
            append_cstr(dst, cap, &len, "-");
        }
        append_cstr(dst, cap, &len, "\n");
    }

    return len;
}

static uint32_t vgen_pci(char *dst, uint32_t cap) {
    uint32_t len = 0;
    pci_device_t devs[PCI_MAX_DEVICES];
    int count = pci_get_devices(devs, PCI_MAX_DEVICES);

    append_cstr(dst, cap, &len, "PCI devices (");
    append_dec_u32(dst, cap, &len, (uint32_t)count);
    append_cstr(dst, cap, &len, "):\n");

    for (int i = 0; i < count; i++) {
        pci_device_t *d = &devs[i];
        append_cstr(dst, cap, &len, "  ");
        append_dec_u32(dst, cap, &len, d->bus);
        append_cstr(dst, cap, &len, ":");
        append_dec_u32(dst, cap, &len, d->device);
        append_cstr(dst, cap, &len, ".");
        append_dec_u32(dst, cap, &len, d->function);
        append_cstr(dst, cap, &len, " vendor=");
        append_hex_u32(dst, cap, &len, d->vendor_id);
        append_cstr(dst, cap, &len, " device=");
        append_hex_u32(dst, cap, &len, d->device_id);
        append_cstr(dst, cap, &len, " class=");
        append_hex_u32(dst, cap, &len, d->class_code);
        append_cstr(dst, cap, &len, ".");
        append_hex_u32(dst, cap, &len, d->subclass);
        if (d->irq_line && d->irq_line != 0xFF) {
            append_cstr(dst, cap, &len, " irq=");
            append_dec_u32(dst, cap, &len, d->irq_line);
        }
        append_cstr(dst, cap, &len, "\n");
    }

    return len;
}

static int vfile_read_from_generated(vgen_fn_t gen, uint32_t offset, void *buf, uint32_t len) {
    if (!buf || len == 0) return 0;
    uint32_t total = gen(vgen_buf, sizeof(vgen_buf));
    if (offset >= total) return 0;
    uint32_t remaining = total - offset;
    if (len > remaining) len = remaining;
    memcpy(buf, vgen_buf + offset, len);
    return (int)len;
}

static uint32_t vfile_size_from_generated(vgen_fn_t gen) {
    return gen(vgen_buf, sizeof(vgen_buf));
}

static uint32_t vfile_kdebug_size(void) {
    return klog_snapshot_size();
}

static int vfile_kdebug_read(uint32_t offset, void *buf, uint32_t len) {
    return klog_read_bytes(offset, buf, len);
}

static uint32_t vfile_meminfo_size(void) {
    return vfile_size_from_generated(vgen_meminfo);
}

static int vfile_meminfo_read(uint32_t offset, void *buf, uint32_t len) {
    return vfile_read_from_generated(vgen_meminfo, offset, buf, len);
}

static uint32_t vfile_cpuinfo_size(void) {
    return vfile_size_from_generated(vgen_cpuinfo);
}

static int vfile_cpuinfo_read(uint32_t offset, void *buf, uint32_t len) {
    return vfile_read_from_generated(vgen_cpuinfo, offset, buf, len);
}

static uint32_t vfile_lsirq_size(void) {
    return vfile_size_from_generated(vgen_lsirq);
}

static int vfile_lsirq_read(uint32_t offset, void *buf, uint32_t len) {
    return vfile_read_from_generated(vgen_lsirq, offset, buf, len);
}

static uint32_t vfile_pci_size(void) {
    return vfile_size_from_generated(vgen_pci);
}

static int vfile_pci_read(uint32_t offset, void *buf, uint32_t len) {
    return vfile_read_from_generated(vgen_pci, offset, buf, len);
}

static int vfs_register_virtual_file(const char *name,
                                     uint32_t (*size_fn)(void),
                                     int (*read_fn)(uint32_t, void *, uint32_t)) {
    if (!name || !size_fn || !read_fn) return -1;
    if (virtual_file_count >= VFS_MAX_VIRTUAL_FILES) return -1;
    virtual_files[virtual_file_count].name = name;
    virtual_files[virtual_file_count].size_fn = size_fn;
    virtual_files[virtual_file_count].read_fn = read_fn;
    virtual_file_count++;
    return 0;
}

static int vfs_path_matches_virtual(const char *path, const char *name) {
    if (!path || !name) return 0;
    if (strcmp(path, name) == 0) return 1;
    if (path[0] == '/' && strcmp(path + 1, name) == 0) return 1;
    return 0;
}

static int vfs_find_virtual_file(const char *path) {
    for (int i = 0; i < virtual_file_count; i++) {
        if (vfs_path_matches_virtual(path, virtual_files[i].name)) {
            return i;
        }
    }
    return -1;
}

static int vfs_virtual_fs_id_from_index(int index) {
    return VFS_VIRT_BASE_ID - index;
}

static int vfs_virtual_index_from_fs_id(int fs_id) {
    int index = VFS_VIRT_BASE_ID - fs_id;
    if (index < 0 || index >= virtual_file_count) return -1;
    return index;
}

void vfs_init(void) {
    fs_count = 0;
    memset(filesystems, 0, sizeof(filesystems));
    virtual_file_count = 0;
    memset(virtual_files, 0, sizeof(virtual_files));

    vfs_register_virtual_file("kdebug.mos", vfile_kdebug_size, vfile_kdebug_read);
    vfs_register_virtual_file("kmeminfo.mos", vfile_meminfo_size, vfile_meminfo_read);
    vfs_register_virtual_file("kcpuinfo.mos", vfile_cpuinfo_size, vfile_cpuinfo_read);
    vfs_register_virtual_file("kirq.mos", vfile_lsirq_size, vfile_lsirq_read);
    vfs_register_virtual_file("kpci.mos", vfile_pci_size, vfile_pci_read);
}

int vfs_register_fs(const vfs_fs_ops_t *ops) {
    if (!ops || fs_count >= VFS_MAX_FILESYSTEMS) return -1;
    int id = fs_count;
    filesystems[fs_count++] = ops;
    printf("[vfs] registered '%s' as fs %d\n", ops->name, id);
    return id;
}

int vfs_open(vfs_fd_table_t *fdt, const char *path, int flags) {
    if (!fdt || !path) return -1;

    int fd = -1;
    for (int i = 0; i < VFS_MAX_FDS_PER_TASK; i++) {
        if (!fdt->fds[i].in_use) { fd = i; break; }
    }
    if (fd < 0) return -1;

    int vfi = vfs_find_virtual_file(path);
    if (vfi >= 0) {
        int access = flags & 0x3;
        if (access != O_RDONLY) return -1;
        fdt->fds[fd].in_use = 1;
        fdt->fds[fd].fs_id = vfs_virtual_fs_id_from_index(vfi);
        fdt->fds[fd].fs_handle = 0;
        return fd;
    }

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->open) continue;
        int handle = filesystems[fs]->open(path, flags);
        if (handle >= 0) {
            fdt->fds[fd].in_use = 1;
            fdt->fds[fd].fs_id = fs;
            fdt->fds[fd].fs_handle = handle;
            return fd;
        }
    }

    return -1;
}

int vfs_read(vfs_fd_table_t *fdt, int fd, void *buf, uint32_t len) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    int vfi = vfs_virtual_index_from_fs_id(fs);
    if (vfi >= 0) {
        int n = virtual_files[vfi].read_fn((uint32_t)fdt->fds[fd].fs_handle, buf, len);
        if (n > 0) fdt->fds[fd].fs_handle += n;
        return n;
    }

    if (!filesystems[fs]->read) return -1;
    return filesystems[fs]->read(fdt->fds[fd].fs_handle, buf, len);
}

int vfs_write(vfs_fd_table_t *fdt, int fd, const void *buf, uint32_t len) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    int vfi = vfs_virtual_index_from_fs_id(fs);
    if (vfi >= 0) return -1;

    if (!filesystems[fs]->write) return -1;
    return filesystems[fs]->write(fdt->fds[fd].fs_handle, buf, len);
}

int vfs_close(vfs_fd_table_t *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    int ret = 0;
    if (vfs_virtual_index_from_fs_id(fs) < 0) {
        if (fs >= 0 && filesystems[fs]->close) {
            ret = filesystems[fs]->close(fdt->fds[fd].fs_handle);
        }
    }

    fdt->fds[fd].in_use = 0;
    fdt->fds[fd].fs_id = 0;
    fdt->fds[fd].fs_handle = 0;
    return ret;
}

int vfs_seek(vfs_fd_table_t *fdt, int fd, int offset, int whence) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    int vfi = vfs_virtual_index_from_fs_id(fs);
    if (vfi >= 0) {
        int size = (int)virtual_files[vfi].size_fn();
        int pos;
        switch (whence) {
            case SEEK_SET: pos = offset; break;
            case SEEK_CUR: pos = fdt->fds[fd].fs_handle + offset; break;
            case SEEK_END: pos = size + offset; break;
            default: return -1;
        }
        if (pos < 0) pos = 0;
        if (pos > size) pos = size;
        fdt->fds[fd].fs_handle = pos;
        return pos;
    }

    if (!filesystems[fs]->seek) return -1;
    return filesystems[fs]->seek(fdt->fds[fd].fs_handle, offset, whence);
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return -1;

    int vfi = vfs_find_virtual_file(path);
    if (vfi >= 0) {
        st->size = virtual_files[vfi].size_fn();
        st->type = VFS_FILE;
        return 0;
    }

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->stat) continue;
        if (filesystems[fs]->stat(path, st) == 0) return 0;
    }
    return -1;
}

int vfs_readdir(const char *path, int index, char *buf, uint32_t size) {
    if (!path || !buf || size == 0 || index < 0) return 0;

    int remaining = index;
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        if (remaining < virtual_file_count) {
            const char *name = virtual_files[remaining].name;
            size_t n = strlen(name);
            if (n >= size) n = size - 1;
            memcpy(buf, name, n);
            buf[n] = '\0';
            return (int)(n + 1);
        }
        remaining -= virtual_file_count;
    }

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->readdir) continue;

        int local = 0;
        while (1) {
            int ret = filesystems[fs]->readdir(path, local, buf, size);
            if (ret <= 0) break;
            if (remaining == 0) return ret;
            remaining--;
            local++;
        }
    }

    return 0;
}

int vfs_unlink(const char *path) {
    if (!path) return -1;
    if (vfs_find_virtual_file(path) >= 0) return -1;

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->unlink) continue;
        if (filesystems[fs]->unlink(path) == 0) return 0;
    }
    return -1;
}

int vfs_read_file(const char *path, void **out_data, uint32_t *out_size) {
    if (!path || !out_data || !out_size) return -1;

    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) return -1;
    if (st.size == 0) return -1;

    void *buf = kmalloc(st.size);
    if (!buf) return -1;

    vfs_fd_table_t tmp;
    memset(&tmp, 0, sizeof(tmp));

    int fd = vfs_open(&tmp, path, O_RDONLY);
    if (fd < 0) {
        kfree(buf);
        return -1;
    }

    uint32_t total = 0;
    while (total < st.size) {
        int n = vfs_read(&tmp, fd, (uint8_t *)buf + total, st.size - total);
        if (n <= 0) break;
        total += (uint32_t)n;
    }

    vfs_close(&tmp, fd);

    if (total == 0) {
        kfree(buf);
        return -1;
    }

    *out_data = buf;
    *out_size = total;
    return 0;
}

void vfs_close_all(vfs_fd_table_t *fdt) {
    if (!fdt) return;
    for (int i = 0; i < VFS_MAX_FDS_PER_TASK; i++) {
        if (fdt->fds[i].in_use) {
            vfs_close(fdt, i);
        }
    }
}
