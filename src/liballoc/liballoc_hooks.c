#include <stddef.h>
#include <stdint.h>
#include "../arch/i686/cpu.h"
#include "../memlayout.h"
#include "liballoc_hooks.h"

// Bump allocator for liballoc hooks
// This is the simplest allocator - just advances heap pointer, no freeing

// Heap region: starts at 4MB (after identity-mapped region)
// and extends for 2MB
static uintptr_t heap_current = KERNEL_HEAP_START;
static uintptr_t heap_end = KERNEL_HEAP_END;

#define PAGE_SIZE 4096

// Track interrupt state for nested lock/unlock
static int interrupt_state = 0;

/**
 * Lock memory data structures by disabling interrupts
 * @return 0 on success
 */
int liballoc_lock(void) {
    interrupt_state = cpu_interrupts_enabled();
    cpu_disable_interrupts();
    return 0;
}

/**
 * Unlock memory data structures by restoring interrupt state
 * @return 0 on success
 */
int liballoc_unlock(void) {
    if (interrupt_state) {
        cpu_enable_interrupts();
    }
    return 0;
}

/**
 * Allocate pages using bump allocator
 * @param num_pages Number of 4KB pages to allocate
 * @return Pointer to allocated memory, or NULL if out of memory
 */
void* liballoc_alloc(size_t num_pages) {
    if (num_pages == 0) {
        return NULL;
    }

    // Calculate size needed
    size_t size = num_pages * PAGE_SIZE;

    // Check if we have enough space
    if (heap_current + size > heap_end) {
        return NULL;  // Out of memory
    }

    // Allocate by advancing the heap pointer
    void* ptr = (void*)heap_current;
    heap_current += size;

    return ptr;
}

/**
 * Free pages (no-op for bump allocator)
 * @param ptr Pointer to memory to free
 * @param num_pages Number of pages to free
 * @return 0 on success
 */
int liballoc_free(void* ptr, size_t num_pages) {
    // Bump allocator doesn't support freeing
    // Memory is reclaimed only when the entire heap is reset
    (void)ptr;
    (void)num_pages;
    return 0;
}

void liballoc_heap_info(uint32_t *start, uint32_t *end, uint32_t *current) {
    if (start) *start = (uint32_t)KERNEL_HEAP_START;
    if (end) *end = (uint32_t)heap_end;
    if (current) *current = (uint32_t)heap_current;
}

