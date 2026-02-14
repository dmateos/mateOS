#ifndef _FAT16_H
#define _FAT16_H

#include "vfs.h"

// Initialize FAT16 backend on ATA PIO disk.
// Returns 0 when mounted, -1 otherwise.
int fat16_init(void);

// Access VFS backend ops for FAT16.
const vfs_fs_ops_t *fat16_get_ops(void);

#endif
