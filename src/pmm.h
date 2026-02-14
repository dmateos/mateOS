#ifndef _PMM_H
#define _PMM_H

#include "lib.h"

// Physical frame allocator
// Manages 4KB frames from 8MB to 32MB (6144 frames)
#define PMM_START      0x800000   // 8MB - below is kernel/heap/boot
#define PMM_END        0x2000000  // 32MB
#define PMM_FRAME_SIZE 0x1000     // 4KB
#define PMM_FRAME_COUNT ((PMM_END - PMM_START) / PMM_FRAME_SIZE)  // 6144

void pmm_init(void);

// Allocate a single 4KB frame, returns physical address or 0 on failure
uint32_t pmm_alloc_frame(void);

// Free a single 4KB frame
void pmm_free_frame(uint32_t physical_addr);

// Allocate count contiguous frames, returns physical address of first or 0
uint32_t pmm_alloc_frames(uint32_t count);

// Free count contiguous frames starting at physical_addr
void pmm_free_frames(uint32_t physical_addr, uint32_t count);

void pmm_get_stats(uint32_t *total, uint32_t *used, uint32_t *free_frames);

#endif
