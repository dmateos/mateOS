#ifndef _RAMFS_H
#define _RAMFS_H

#include "lib.h"

// Maximum number of files in ramfs
#define RAMFS_MAX_FILES 16

// Maximum filename length
#define RAMFS_NAME_MAX 32

// Ramfs file entry
typedef struct {
  char name[RAMFS_NAME_MAX];
  uint32_t data;    // Pointer to file data in memory
  uint32_t size;    // File size in bytes
  int in_use;       // 1 if entry is valid, 0 if free
} ramfs_file_t;

// Initialize ramfs from initrd module
// Format: simple archive with header: [name_len][name][size][data]
void ramfs_init(void *initrd_start, uint32_t initrd_size);

// Look up a file by name
ramfs_file_t *ramfs_lookup(const char *name);

// List all files
void ramfs_list(void);

// Get file count
int ramfs_get_file_count(void);

// Get file by index (for readdir)
ramfs_file_t *ramfs_get_file_by_index(int index);

// VFS backend
struct vfs_fs_ops;
const struct vfs_fs_ops *ramfs_get_ops(void);

#endif
