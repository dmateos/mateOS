#include <stddef.h>
#include <stdint.h>

// Bump allocator for liballoc hooks
// This is the simplest allocator - just advances heap pointer, no freeing

// Heap region: starts at 4MB (after identity-mapped region)
// and extends for 2MB
static uintptr_t heap_current = 0x400000;  // 4MB mark
static uintptr_t heap_end = 0x600000;      // 6MB mark (2MB heap)

#define PAGE_SIZE 4096

// Track interrupt state for nested lock/unlock
static int interrupt_state = 0;

// Assembly helpers for interrupt control
static inline void cli(void) {
    __asm__ volatile("cli");
}

static inline void sti(void) {
    __asm__ volatile("sti");
}

static inline int interrupts_enabled(void) {
    unsigned int flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags));
    return (flags & 0x200) != 0;  // IF flag is bit 9
}

/**
 * Lock memory data structures by disabling interrupts
 * @return 0 on success
 */
int liballoc_lock(void) {
    interrupt_state = interrupts_enabled();
    cli();
    return 0;
}

/**
 * Unlock memory data structures by restoring interrupt state
 * @return 0 on success
 */
int liballoc_unlock(void) {
    if (interrupt_state) {
        sti();
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
