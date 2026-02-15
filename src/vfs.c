#include "vfs.h"
#include "liballoc/liballoc_1_1.h"
#include "vfs_proc.h"

static const vfs_fs_ops_t *filesystems[VFS_MAX_FILESYSTEMS];
static int fs_count = 0;

#define VFS_MAX_VIRTUAL_FILES 16
#define VFS_VIRT_BASE_ID (-1000)

typedef struct {
    const char *name;
    uint32_t (*size_fn)(void);
    int (*read_fn)(uint32_t offset, void *buf, uint32_t len);
} vfs_virtual_file_t;

static vfs_virtual_file_t virtual_files[VFS_MAX_VIRTUAL_FILES];
static int virtual_file_count = 0;

static void vfs_copy_path(char *dst, const char *src) {
    int i = 0;
    for (; i < VFS_PATH_MAX - 1 && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

int vfs_register_virtual_file(const char *name,
                              uint32_t (*size_fn)(void),
                              int (*read_fn)(uint32_t, void *, uint32_t)) {
    if (!name || !size_fn || !read_fn) return -1;
    if (virtual_file_count >= VFS_MAX_VIRTUAL_FILES) {
        kprintf("[vfs] register virtual failed name=%s err=%d\n", name, -1);
        return -1;
    }
    virtual_files[virtual_file_count].name = name;
    virtual_files[virtual_file_count].size_fn = size_fn;
    virtual_files[virtual_file_count].read_fn = read_fn;
    kprintf("[vfs] register virtual name=%s\n", name);
    virtual_file_count++;
    return 0;
}

int vfs_get_registered_fs_count(void) {
    return fs_count;
}

const char *vfs_get_registered_fs_name(int idx) {
    if (idx < 0 || idx >= fs_count || !filesystems[idx] || !filesystems[idx]->name) {
        return "(null)";
    }
    return filesystems[idx]->name;
}

int vfs_get_virtual_file_count(void) {
    return virtual_file_count;
}

const char *vfs_get_virtual_file_name(int idx) {
    if (idx < 0 || idx >= virtual_file_count || !virtual_files[idx].name) {
        return "(null)";
    }
    return virtual_files[idx].name;
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
    vfs_proc_register_files();
}

int vfs_register_fs(const vfs_fs_ops_t *ops) {
    if (!ops || fs_count >= VFS_MAX_FILESYSTEMS) {
        kprintf("[vfs] register fs failed name=%s err=%d\n",
                (ops && ops->name) ? ops->name : "(null)", -1);
        return -1;
    }
    int id = fs_count;
    filesystems[fs_count++] = ops;
    kprintf("[vfs] registered '%s' as fs %d\n", ops->name, id);
    return id;
}

int vfs_open(vfs_fd_table_t *fdt, const char *path, int flags) {
    if (!fdt || !path) {
        kprintf("[vfs] open fail path=%s err=%d\n", path ? path : "(null)", -1);
        return -1;
    }

    int fd = -1;
    for (int i = 0; i < VFS_MAX_FDS_PER_TASK; i++) {
        if (!fdt->fds[i].in_use) { fd = i; break; }
    }
    if (fd < 0) {
        kprintf("[vfs] open fail path=%s err=%d\n", path, -2);
        return -2;
    }

    int vfi = vfs_find_virtual_file(path);
    if (vfi >= 0) {
        int access = flags & 0x3;
        if (access != O_RDONLY) {
            kprintf("[vfs] open fail path=%s err=%d\n", path, -3);
            return -3;
        }
        fdt->fds[fd].in_use = 1;
        fdt->fds[fd].fs_id = vfs_virtual_fs_id_from_index(vfi);
        fdt->fds[fd].fs_handle = 0;
        vfs_copy_path(fdt->fds[fd].debug_path, path);
        return fd;
    }

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->open) continue;
        int handle = filesystems[fs]->open(path, flags);
        if (handle >= 0) {
            fdt->fds[fd].in_use = 1;
            fdt->fds[fd].fs_id = fs;
            fdt->fds[fd].fs_handle = handle;
            vfs_copy_path(fdt->fds[fd].debug_path, path);
            return fd;
        }
    }

    kprintf("[vfs] open fail path=%s err=%d\n", path, -4);
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

    if (!filesystems[fs]->read) {
        kprintf("[vfs] read fail path=%s err=%d\n", fdt->fds[fd].debug_path, -1);
        return -1;
    }
    int rc = filesystems[fs]->read(fdt->fds[fd].fs_handle, buf, len);
    if (rc < 0) {
        kprintf("[vfs] read fail path=%s err=%d\n", fdt->fds[fd].debug_path, rc);
    }
    return rc;
}

int vfs_write(vfs_fd_table_t *fdt, int fd, const void *buf, uint32_t len) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    int vfi = vfs_virtual_index_from_fs_id(fs);
    if (vfi >= 0) {
        kprintf("[vfs] write fail path=%s err=%d\n", fdt->fds[fd].debug_path, -1);
        return -1;
    }

    if (!filesystems[fs]->write) {
        kprintf("[vfs] write fail path=%s err=%d\n", fdt->fds[fd].debug_path, -2);
        return -2;
    }
    int rc = filesystems[fs]->write(fdt->fds[fd].fs_handle, buf, len);
    if (rc < 0) {
        kprintf("[vfs] write fail path=%s err=%d\n", fdt->fds[fd].debug_path, rc);
    }
    return rc;
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
    fdt->fds[fd].debug_path[0] = '\0';
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
    kprintf("[vfs] stat fail path=%s err=%d\n", path, -1);
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
