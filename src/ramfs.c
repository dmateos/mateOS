#include "ramfs.h"
#include "lib.h"

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

  printf("  Initrd: 0x%x, size=%d bytes\n", (uint32_t)initrd_start, initrd_size);

  uint8_t *ptr = (uint8_t *)initrd_start;
  uint8_t *end = ptr + initrd_size;

  while (ptr < end && file_count < RAMFS_MAX_FILES) {
    // Read name length
    if (ptr + 4 > end) break;
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
    if (ptr + name_len > end) break;
    char name[RAMFS_NAME_MAX];
    memcpy(name, ptr, name_len);
    name[name_len] = '\0';
    ptr += name_len;

    // Read size
    if (ptr + 4 > end) break;
    uint32_t size = *(uint32_t *)ptr;
    ptr += 4;

    // Validate size
    if (ptr + size > end) {
      printf("  ERROR: File '%s' size %d exceeds initrd boundary\n", name, size);
      break;
    }

    // Add file to ramfs
    ramfs_file_t *f = &files[file_count];
    memcpy(f->name, name, name_len + 1);
    f->data = (uint32_t)ptr;
    f->size = size;
    f->in_use = 1;

    printf("  File %d: '%s' at 0x%x, %d bytes\n",
           file_count, f->name, f->data, f->size);

    ptr += size;
    file_count++;
  }

  printf("Ramfs initialized with %d files\n", file_count);
}

ramfs_file_t *ramfs_lookup(const char *name) {
  if (!name) return NULL;

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

int ramfs_get_file_count(void) {
  return file_count;
}

