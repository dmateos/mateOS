#ifndef _PMM_H
#define _PMM_H

#include "lib.h"

// Physical frame allocator
// Manages 4KB frames from PMM_START up to a dynamically detected end.
// Maximum supported: 1GB (PMM_MAX_END) — limited by higher-half VA space.
// The kernel higher-half (0xC0000000-0xFFFFFFFF) can map at most 1GB of
// physical RAM via the PHYS_TO_KVIRT linear offset.
#define PMM_START 0x800000u        // 8MB - below is kernel/heap/boot
#define PMM_MAX_END 0x40000000u    // 1GB cap (higher-half VA limit)
#define PMM_FRAME_SIZE 0x1000u     // 4KB

// Maximum possible frames (used to size the static bitmap)
// (1GB - 8MB) / 4KB = 261120 frames, bitmap = 32640 bytes
#define PMM_MAX_FRAME_COUNT ((PMM_MAX_END - PMM_START) / PMM_FRAME_SIZE)

// Actual end address (set at init time)
extern uint32_t PMM_END;
// Actual frame count (set at init time)
extern uint32_t PMM_FRAME_COUNT;

void pmm_init(uint32_t ram_top);
void pmm_reserve_region(uint32_t start_addr, uint32_t size_bytes);

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
