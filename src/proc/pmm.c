#include "pmm.h"

// Bitmap: 1 bit per frame, 1 = used, 0 = free
// 6144 frames / 8 bits per byte = 768 bytes
static uint8_t frame_bitmap[PMM_FRAME_COUNT / 8];

static inline uint32_t frame_index(uint32_t physical_addr) {
    return (physical_addr - PMM_START) / PMM_FRAME_SIZE;
}

static inline uint32_t frame_addr(uint32_t index) {
    return PMM_START + index * PMM_FRAME_SIZE;
}

static inline int bitmap_test(uint32_t index) {
    return frame_bitmap[index / 8] & (1 << (index % 8));
}

static inline void bitmap_set(uint32_t index) {
    frame_bitmap[index / 8] |= (1 << (index % 8));
}

static inline void bitmap_clear(uint32_t index) {
    frame_bitmap[index / 8] &= ~(1 << (index % 8));
}

void pmm_init(void) {
    // Mark all frames as free
    memset(frame_bitmap, 0, sizeof(frame_bitmap));
    printf("PMM initialized: %d frames (%dMB) from 0x%x to 0x%x\n",
           PMM_FRAME_COUNT, (PMM_FRAME_COUNT * PMM_FRAME_SIZE) / (1024 * 1024),
           PMM_START, PMM_END);
}

void pmm_reserve_region(uint32_t start_addr, uint32_t size_bytes) {
    if (!size_bytes)
        return;
    uint32_t end_addr = start_addr + size_bytes;
    if (end_addr < start_addr)
        end_addr = 0xFFFFFFFFu;

    // Intersect with PMM managed range.
    if (end_addr <= PMM_START || start_addr >= PMM_END)
        return;
    if (start_addr < PMM_START)
        start_addr = PMM_START;
    if (end_addr > PMM_END)
        end_addr = PMM_END;

    uint32_t first = start_addr & ~(PMM_FRAME_SIZE - 1u);
    uint32_t last = (end_addr + PMM_FRAME_SIZE - 1u) & ~(PMM_FRAME_SIZE - 1u);
    for (uint32_t addr = first; addr < last; addr += PMM_FRAME_SIZE) {
        bitmap_set(frame_index(addr));
    }
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < PMM_FRAME_COUNT; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return frame_addr(i);
        }
    }
    printf("[pmm] out of frames!\n");
    return 0;
}

void pmm_free_frame(uint32_t physical_addr) {
    if (physical_addr < PMM_START || physical_addr >= PMM_END)
        return;
    if (physical_addr & (PMM_FRAME_SIZE - 1))
        return; // not aligned
    uint32_t idx = frame_index(physical_addr);
    if (!bitmap_test(idx)) {
        printf("[pmm] double-free detected at 0x%x (frame %d)\n", physical_addr,
               idx);
        return;
    }
    bitmap_clear(idx);
}

uint32_t pmm_alloc_frames(uint32_t count) {
    if (count == 0)
        return 0;
    // Simple linear search for contiguous run
    for (uint32_t i = 0; i <= PMM_FRAME_COUNT - count; i++) {
        uint32_t j;
        for (j = 0; j < count; j++) {
            if (bitmap_test(i + j))
                break;
        }
        if (j == count) {
            // Found contiguous run
            for (j = 0; j < count; j++) {
                bitmap_set(i + j);
            }
            return frame_addr(i);
        }
    }
    printf("[pmm] can't allocate %d contiguous frames\n", count);
    return 0;
}

void pmm_free_frames(uint32_t physical_addr, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        pmm_free_frame(physical_addr + i * PMM_FRAME_SIZE);
    }
}

void pmm_get_stats(uint32_t *total, uint32_t *used, uint32_t *free_frames) {
    uint32_t used_count = 0;
    for (uint32_t i = 0; i < PMM_FRAME_COUNT; i++) {
        if (bitmap_test(i))
            used_count++;
    }
    if (total)
        *total = PMM_FRAME_COUNT;
    if (used)
        *used = used_count;
    if (free_frames)
        *free_frames = PMM_FRAME_COUNT - used_count;
}
