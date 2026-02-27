#include "ramfs.h"
#include "arch/i686/cpu.h"
#include "arch/i686/paging.h"
#include "lib.h"
#include "memlayout.h"
#include "proc/task.h"
#include "vfs.h"

static ramfs_file_t files[RAMFS_MAX_FILES];
static int file_count = 0;

// Simple archive format:
// Archive: [file1][file2]...[EOF marker]
// File:    [name_len:4][name:name_len][size:4][data:size]
// EOF:     [0:4]

void ramfs_init(void *initrd_start, uint32_t initrd_size) {
    printf("Ramfs initializing...\n");

    // Clear file table
    memset(files, 0, sizeof(files));
    file_count = 0;

    if (!initrd_start || initrd_size == 0) {
        printf("  No initrd provided\n");
        return;
    }

    printf("  Initrd: 0x%x, size=%d bytes\n", (uint32_t)initrd_start,
           initrd_size);

    uint8_t *ptr = (uint8_t *)initrd_start;
    uint8_t *end = ptr + initrd_size;

    while (ptr < end && file_count < RAMFS_MAX_FILES) {
        // Read name length
        if (ptr + 4 > end)
            break;
        uint32_t name_len = *(uint32_t *)ptr;
        ptr += 4;

        // EOF marker
        if (name_len == 0) {
            printf("  End of archive\n");
            break;
        }

        // Validate name length
        if (name_len == 0 || name_len >= RAMFS_NAME_MAX) {
            printf("  ERROR: Invalid name length %d\n", name_len);
            break;
        }

        // Read name
        if (ptr + name_len > end)
            break;
        char name[RAMFS_NAME_MAX];
        memcpy(name, ptr, name_len);
        name[name_len] = '\0';
        ptr += name_len;

        // Read size
        if (ptr + 4 > end)
            break;
        uint32_t size = *(uint32_t *)ptr;
        ptr += 4;

        // Validate size
        if (ptr + size > end) {
            printf("  ERROR: File '%s' size %d exceeds initrd boundary\n", name,
                   size);
            break;
        }

        // Add file to ramfs
        ramfs_file_t *f = &files[file_count];
        memcpy(f->name, name, name_len + 1);
        f->data = (uint32_t)ptr;
        f->size = size;
        f->in_use = 1;

        printf("  File %d: '%s' at 0x%x, %d bytes\n", file_count, f->name,
               f->data, f->size);

        ptr += size;
        file_count++;
    }

    printf("Ramfs initialized with %d files\n", file_count);
}

ramfs_file_t *ramfs_lookup(const char *name) {
    if (!name)
        return NULL;
    // Strip leading slashes — ramfs is flat, no directories
    while (name[0] == '/')
        name++;

    for (int i = 0; i < file_count; i++) {
        if (files[i].in_use && strcmp(files[i].name, name) == 0) {
            return &files[i];
        }
    }

    return NULL;
}

void ramfs_list(void) {
    printf("Ramfs files (%d total):\n", file_count);

    if (file_count == 0) {
        printf("  (empty)\n");
        return;
    }

    for (int i = 0; i < file_count; i++) {
        if (files[i].in_use) {
            printf("  %s (%d bytes)\n", files[i].name, files[i].size);
        }
    }
}

int ramfs_get_file_count(void) { return file_count; }

ramfs_file_t *ramfs_get_file_by_index(int index) {
    if (index < 0 || index >= file_count)
        return NULL;
    if (!files[index].in_use)
        return NULL;
    return &files[index];
}

// --- VFS backend ---

#define RAMFS_MAX_OPEN 16

static struct {
    int file_idx;
    uint32_t offset;
    int in_use;
} ramfs_open_files[RAMFS_MAX_OPEN];

static int ramfs_vfs_open(const char *path, int flags) {
    (void)flags;
    ramfs_file_t *f = ramfs_lookup(path);
    if (!f)
        return -1;

    // Find file index
    int file_idx = -1;
    for (int i = 0; i < file_count; i++) {
        if (&files[i] == f) {
            file_idx = i;
            break;
        }
    }
    if (file_idx < 0)
        return -1;

    // Find free open slot
    for (int i = 0; i < RAMFS_MAX_OPEN; i++) {
        if (!ramfs_open_files[i].in_use) {
            ramfs_open_files[i].in_use = 1;
            ramfs_open_files[i].file_idx = file_idx;
            ramfs_open_files[i].offset = 0;
            return i;
        }
    }
    return -1; // Too many open files
}

static int ramfs_vfs_read(int handle, void *buf, uint32_t len) {
    if (handle < 0 || handle >= RAMFS_MAX_OPEN)
        return -1;
    if (!ramfs_open_files[handle].in_use)
        return -1;

    int idx = ramfs_open_files[handle].file_idx;
    ramfs_file_t *f = &files[idx];
    uint32_t off = ramfs_open_files[handle].offset;

    if (off >= f->size)
        return 0; // EOF

    uint32_t avail = f->size - off;
    if (len > avail)
        len = avail;

    uint32_t src_addr = f->data + off;
    // When reading into a user buffer while running on that task's page tables,
    // source initrd data and destination may alias different physical mappings
    // at the same virtual addresses. Bounce through a kernel buffer.
    //
    // Important: kernel code also uses vfs_read() while a user task is current
    // (e.g. spawn/ELF loading into PMM-backed temp buffers). Those kernel reads
    // pass physical pointers that numerically overlap the user VA range, so
    // only use the bounce path when executing under the current task's user
    // CR3.
    task_t *cur = task_current();
    uint32_t dst = (uint32_t)buf;
    int dst_in_user_region =
        (dst >= USER_REGION_START && dst < USER_REGION_END);
    int on_current_user_cr3 =
        cur && cur->page_dir && get_cr3() == (uint32_t)cur->page_dir;
    if (dst_in_user_region && on_current_user_cr3) {
        enum { BOUNCE_SZ = 4096 };
        static uint8_t bounce[BOUNCE_SZ];
        uint32_t done = 0;
        uint32_t restore_cr3 = get_cr3();
        // Disable interrupts during the bounce copy to prevent preemption —
        // the static bounce buffer is shared and would be corrupted if another
        // task's ramfs_vfs_read ran between the two memcpy calls.
        uint32_t irq = cpu_irq_save();
        while (done < len) {
            uint32_t chunk = len - done;
            if (chunk > BOUNCE_SZ)
                chunk = BOUNCE_SZ;

            paging_switch(paging_get_kernel_dir());
            memcpy(bounce, (void *)(src_addr + done), chunk);

            paging_switch((page_directory_t *)restore_cr3);
            memcpy((uint8_t *)buf + done, bounce, chunk);

            done += chunk;
        }
        cpu_irq_restore(irq);
    } else {
        memcpy(buf, (void *)src_addr, len);
    }

    ramfs_open_files[handle].offset += len;
    return (int)len;
}

static int ramfs_vfs_write(int handle, const void *buf, uint32_t len) {
    (void)handle;
    (void)buf;
    (void)len;
    return -1; // Read-only filesystem
}

static int ramfs_vfs_close(int handle) {
    if (handle < 0 || handle >= RAMFS_MAX_OPEN)
        return -1;
    if (!ramfs_open_files[handle].in_use)
        return -1;
    ramfs_open_files[handle].in_use = 0;
    return 0;
}

static int ramfs_vfs_seek(int handle, int offset, int whence) {
    if (handle < 0 || handle >= RAMFS_MAX_OPEN)
        return -1;
    if (!ramfs_open_files[handle].in_use)
        return -1;

    int idx = ramfs_open_files[handle].file_idx;
    uint32_t size = files[idx].size;
    int pos;

    switch (whence) {
    case SEEK_SET:
        pos = offset;
        break;
    case SEEK_CUR:
        pos = (int)ramfs_open_files[handle].offset + offset;
        break;
    case SEEK_END:
        pos = (int)size + offset;
        break;
    default:
        return -1;
    }

    if (pos < 0)
        pos = 0;
    if ((uint32_t)pos > size)
        pos = (int)size;

    ramfs_open_files[handle].offset = (uint32_t)pos;
    return pos;
}

static int ramfs_vfs_stat(const char *path, vfs_stat_t *st) {
    ramfs_file_t *f = ramfs_lookup(path);
    if (!f)
        return -1;
    st->size = f->size;
    st->type = VFS_FILE;
    return 0;
}

static int ramfs_vfs_readdir(const char *path, int index, char *buf,
                             uint32_t size) {
    // ramfs is flat — only list entries when viewing root directory
    if (path && path[0] != '\0' && strcmp(path, "/") != 0)
        return 0;
    ramfs_file_t *f = ramfs_get_file_by_index(index);
    if (!f)
        return 0;

    size_t name_len = strlen(f->name);
    if (name_len >= size)
        name_len = size - 1;
    memcpy(buf, f->name, name_len);
    buf[name_len] = '\0';
    return (int)(name_len + 1);
}

static const vfs_fs_ops_t ramfs_ops = {
    .name = "ramfs",
    .open = ramfs_vfs_open,
    .read = ramfs_vfs_read,
    .write = ramfs_vfs_write,
    .close = ramfs_vfs_close,
    .seek = ramfs_vfs_seek,
    .stat = ramfs_vfs_stat,
    .readdir = ramfs_vfs_readdir,
    .unlink = NULL,
};

const vfs_fs_ops_t *ramfs_get_ops(void) { return &ramfs_ops; }
