#include "vfs.h"
#include "liballoc/liballoc_1_1.h"

static const vfs_fs_ops_t *filesystems[VFS_MAX_FILESYSTEMS];
static int fs_count = 0;

void vfs_init(void) {
    fs_count = 0;
    memset(filesystems, 0, sizeof(filesystems));
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

    // Find a free fd slot
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FDS_PER_TASK; i++) {
        if (!fdt->fds[i].in_use) { fd = i; break; }
    }
    if (fd < 0) return -1;  // No free fds

    // Try each filesystem until one succeeds
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

    return -1;  // No filesystem could open the file
}

int vfs_read(vfs_fd_table_t *fdt, int fd, void *buf, uint32_t len) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    if (!filesystems[fs]->read) return -1;
    return filesystems[fs]->read(fdt->fds[fd].fs_handle, buf, len);
}

int vfs_write(vfs_fd_table_t *fdt, int fd, const void *buf, uint32_t len) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    if (!filesystems[fs]->write) return -1;
    return filesystems[fs]->write(fdt->fds[fd].fs_handle, buf, len);
}

int vfs_close(vfs_fd_table_t *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FDS_PER_TASK) return -1;
    if (!fdt->fds[fd].in_use) return -1;

    int fs = fdt->fds[fd].fs_id;
    int ret = 0;
    if (filesystems[fs]->close) {
        ret = filesystems[fs]->close(fdt->fds[fd].fs_handle);
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
    if (!filesystems[fs]->seek) return -1;
    return filesystems[fs]->seek(fdt->fds[fd].fs_handle, offset, whence);
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return -1;

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->stat) continue;
        if (filesystems[fs]->stat(path, st) == 0) return 0;
    }
    return -1;
}

int vfs_readdir(const char *path, int index, char *buf, uint32_t size) {
    if (!buf || size == 0) return 0;

    for (int fs = 0; fs < fs_count; fs++) {
        if (!filesystems[fs]->readdir) continue;
        int ret = filesystems[fs]->readdir(path, index, buf, size);
        if (ret > 0) return ret;
    }
    return 0;
}

int vfs_read_file(const char *path, void **out_data, uint32_t *out_size) {
    if (!path || !out_data || !out_size) return -1;

    // Get file size via stat
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) return -1;
    if (st.size == 0) return -1;

    // Allocate buffer
    void *buf = kmalloc(st.size);
    if (!buf) return -1;

    // Use a temporary fd table for kernel-internal reads
    vfs_fd_table_t tmp;
    memset(&tmp, 0, sizeof(tmp));

    int fd = vfs_open(&tmp, path, O_RDONLY);
    if (fd < 0) {
        kfree(buf);
        return -1;
    }

    // Read entire file
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
