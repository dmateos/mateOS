#ifndef _VFS_H
#define _VFS_H

#include "lib.h"

#define VFS_MAX_FDS_PER_TASK 16
#define VFS_PATH_MAX 64
#define VFS_MAX_FILESYSTEMS 4

// File types
#define VFS_FILE    0
#define VFS_DIR     1

// Open flags
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     4
#define O_TRUNC     8

// Seek whence
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

// Stat result
typedef struct {
    uint32_t size;
    uint32_t type;    // VFS_FILE or VFS_DIR
} vfs_stat_t;

// Filesystem operations - each FS backend implements these
typedef struct vfs_fs_ops {
    const char *name;
    int (*open)(const char *path, int flags);
    int (*read)(int handle, void *buf, uint32_t len);
    int (*write)(int handle, const void *buf, uint32_t len);
    int (*close)(int handle);
    int (*seek)(int handle, int offset, int whence);
    int (*stat)(const char *path, vfs_stat_t *st);
    int (*readdir)(const char *path, int index, char *buf, uint32_t size);
    int (*unlink)(const char *path);
} vfs_fs_ops_t;

// Open file descriptor (kernel-side)
typedef struct {
    int in_use;
    int fs_id;
    int fs_handle;
    char debug_path[VFS_PATH_MAX];
} vfs_fd_t;

// Per-task file descriptor table
typedef struct {
    vfs_fd_t fds[VFS_MAX_FDS_PER_TASK];
} vfs_fd_table_t;

// Init / register
void vfs_init(void);
int vfs_register_fs(const vfs_fs_ops_t *ops);
int vfs_register_virtual_file(const char *name,
                              uint32_t (*size_fn)(void),
                              int (*read_fn)(uint32_t offset, void *buf, uint32_t len));
int vfs_get_registered_fs_count(void);
const char *vfs_get_registered_fs_name(int idx);
int vfs_get_virtual_file_count(void);
const char *vfs_get_virtual_file_name(int idx);

// File operations (take task's fd table)
int vfs_open(vfs_fd_table_t *fdt, const char *path, int flags);
int vfs_read(vfs_fd_table_t *fdt, int fd, void *buf, uint32_t len);
int vfs_write(vfs_fd_table_t *fdt, int fd, const void *buf, uint32_t len);
int vfs_close(vfs_fd_table_t *fdt, int fd);
int vfs_seek(vfs_fd_table_t *fdt, int fd, int offset, int whence);
int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_readdir(const char *path, int index, char *buf, uint32_t size);
int vfs_unlink(const char *path);

// Helper: read entire file into kmalloc'd buffer (caller must kfree)
int vfs_read_file(const char *path, void **out_data, uint32_t *out_size);

// Helper: close all fds in a table
void vfs_close_all(vfs_fd_table_t *fdt);

#endif
