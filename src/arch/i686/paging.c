#include "paging.h"
#include "lib.h"
#include "memlayout.h"
#include "proc/pmm.h"

// Assembly functions to manipulate control registers
extern void enable_paging(uint32_t page_directory_physical);

// Number of page tables we use (each covers 4MB)
#define NUM_PAGE_TABLES 8
// Store pointers for later modification
static page_directory_t *current_page_dir = NULL;
static page_table_t *current_page_tables = NULL;

// VBE framebuffer page directory entries to propagate to child processes
#define VBE_MAX_DIR_ENTRIES 4
static uint32_t vbe_dir_indices[VBE_MAX_DIR_ENTRIES];
static int vbe_dir_count = 0;

void init_paging(page_directory_t *page_dir, page_table_t *page_tables) {
    printf("Paging initialization starting\n");

    // Store pointers for later modification
    current_page_dir = page_dir;
    current_page_tables = page_tables;

    // Verify alignment - page directory must be 4KB aligned
    uint32_t pd_addr = (uint32_t)page_dir;
    if (pd_addr & 0xFFF) {
        printf("ERROR: Page directory not 4KB aligned (0x%x)\n", pd_addr);
        return;
    }

    // Clear page directory
    memset(page_dir, 0, sizeof(page_directory_t));

    // Set up page tables for identity mapping
    // Table 0: 0x000000 - 0x3FFFFF (first 4MB - kernel, VGA, etc)
    // Table 1: 0x400000 - 0x7FFFFF (second 4MB - heap region)
    for (uint32_t t = 0; t < NUM_PAGE_TABLES; t++) {
        page_table_t *pt = &page_tables[t];
        uint32_t pt_addr = (uint32_t)pt;

        if (pt_addr & 0xFFF) {
            printf("ERROR: Page table %d not 4KB aligned (0x%x)\n", t, pt_addr);
            return;
        }

        // Clear page table
        memset(pt, 0, sizeof(page_table_t));

        // Identity map this 4MB region
        for (uint32_t i = 0; i < 1024; i++) {
            // Physical address = table_number * 4MB + page_index * 4KB
            uint32_t physical_addr = (t * 0x400000) + (i * 0x1000);
            // Set present, writable, supervisor mode
            pt->pages[i] = physical_addr | PAGE_PRESENT | PAGE_WRITE;
        }

        // Set up page directory entry to point to this page table
        page_dir->tables[t] = pt_addr | PAGE_PRESENT | PAGE_WRITE;
    }

    printf("Identity mapped first %d MB\n", NUM_PAGE_TABLES * 4);
    printf(
        "[paging-map] kernel shared (identity): 0x00000000-0x%08x (%d MiB)\n",
        (NUM_PAGE_TABLES * 0x400000) - 1, NUM_PAGE_TABLES * 4);
    printf("[paging-map] table1 split: heap(shared)=0x%08x-0x%08x, "
           "user(per-proc)=0x%08x-0x%08x\n",
           KERNEL_HEAP_START, KERNEL_HEAP_END - 1, USER_REGION_START,
           USER_REGION_END - 1);
    printf("[paging-map] user stack default: 0x%08x-0x%08x (%d pages, "
           "down-growth)\n",
           USER_STACK_BASE_VADDR, USER_STACK_TOP_PAGE_VADDR + 0x0FFF,
           USER_STACK_PAGES);
    printf("Page directory at 0x%x\n", page_dir);
    printf("Enabling paging...\n");

    // Enable paging by loading CR3 and setting CR0.PG
    enable_paging((uint32_t)page_dir);

    printf("Paging enabled successfully!\n");
    printf("CR3 = 0x%x\n", get_cr3());
}

// Mark a page as user-accessible
void paging_set_user(uint32_t virtual_addr) {
    if (!current_page_tables) {
        printf("ERROR: Paging not initialized\n");
        return;
    }

    // Calculate which page table and which page within it
    uint32_t table_idx = virtual_addr / 0x400000; // Each table covers 4MB
    uint32_t page_idx = (virtual_addr % 0x400000) / 0x1000; // 4KB pages

    if (table_idx >= NUM_PAGE_TABLES) {
        printf("ERROR: Address 0x%x outside mapped region\n", virtual_addr);
        return;
    }

    // Add PAGE_USER flag to the page table entry
    current_page_tables[table_idx].pages[page_idx] |= PAGE_USER;

    // Also need to mark the page directory entry as user-accessible
    current_page_dir->tables[table_idx] |= PAGE_USER;

    // Invalidate TLB entry for this page
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

// Get current page tables
page_table_t *paging_get_tables(void) { return current_page_tables; }

// Return the boot/kernel page directory
page_directory_t *paging_get_kernel_dir(void) { return current_page_dir; }

// Map VBE framebuffer into kernel page directory
// Pages are identity-mapped at the VBE physical address
void paging_map_vbe(uint32_t phys_addr, uint32_t size) {
    if (!current_page_dir || !phys_addr || !size)
        return;

    uint32_t start = phys_addr & ~0xFFF;
    uint32_t end = (phys_addr + size + 0xFFF) & ~0xFFF;

    vbe_dir_count = 0;

    for (uint32_t addr = start; addr < end; addr += 0x1000) {
        paging_map_page(current_page_dir, addr, addr,
                        PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

        // Track which directory entries we touched
        uint32_t dir_idx = addr >> 22;
        int found = 0;
        for (int i = 0; i < vbe_dir_count; i++) {
            if (vbe_dir_indices[i] == dir_idx) {
                found = 1;
                break;
            }
        }
        if (!found && vbe_dir_count < VBE_MAX_DIR_ENTRIES) {
            vbe_dir_indices[vbe_dir_count++] = dir_idx;
        }
    }

    printf("[paging] VBE mapped: 0x%x-0x%x (%d pages, %d dir entries)\n", start,
           end, (end - start) / 0x1000, vbe_dir_count);
}

// Create a new address space for a user process
// Shares kernel page tables (tables 0-7) but allocates a private copy
// of page table 1 so user code region (0x700000+) is per-process
page_directory_t *paging_create_address_space(void) {
    // Allocate a page-aligned page directory from PMM
    uint32_t pd_phys = pmm_alloc_frame();
    if (!pd_phys) {
        printf("[paging] failed to allocate page directory\n");
        return NULL;
    }

    page_directory_t *new_dir = (page_directory_t *)pd_phys;
    memset(new_dir, 0, sizeof(page_directory_t));

    // Copy kernel page directory entries (shared kernel page tables)
    for (uint32_t i = 0; i < NUM_PAGE_TABLES; i++) {
        new_dir->tables[i] = current_page_dir->tables[i];
    }

    // Copy VBE framebuffer page directory entries (if any)
    for (int i = 0; i < vbe_dir_count; i++) {
        uint32_t idx = vbe_dir_indices[i];
        new_dir->tables[idx] = current_page_dir->tables[idx];
    }

    // Page table 1 covers 0x400000-0x7FFFFF which includes both:
    //   - Kernel heap (0x400000-0x5FFFFF) - must be shared
    //   - User code region (0x700000-0x7FFFFF) - must be per-process
    // Allocate a private copy of page table 1
    uint32_t pt1_phys = pmm_alloc_frame();
    if (!pt1_phys) {
        printf("[paging] failed to allocate page table 1 copy\n");
        pmm_free_frame(pd_phys);
        return NULL;
    }

    page_table_t *new_pt1 = (page_table_t *)pt1_phys;

    // Copy kernel's page table 1 (preserves heap mappings)
    memcpy(new_pt1, &current_page_tables[1], sizeof(page_table_t));

    // Clear user code region entries (0x700000-0x7FFFFF = entries 768-1023)
    // These will be filled by exec when loading ELF segments
    for (uint32_t i = 768; i < 1024; i++) {
        new_pt1->pages[i] = 0;
    }

    // Point directory entry 1 to our private page table
    new_dir->tables[1] = pt1_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    return new_dir;
}

// Map a virtual page to a physical frame in a specific page directory.
// Returns 0 on success, -1 on OOM (page table allocation failed).
int paging_map_page(page_directory_t *page_dir, uint32_t virtual_addr,
                    uint32_t physical_addr, uint32_t flags) {
    uint32_t dir_idx = virtual_addr >> 22;             // Top 10 bits
    uint32_t table_idx = (virtual_addr >> 12) & 0x3FF; // Next 10 bits

    // Get or create page table
    if (!(page_dir->tables[dir_idx] & PAGE_PRESENT)) {
        // Allocate a new page table
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) {
            printf("[paging] failed to allocate page table\n");
            return -1;
        }
        memset((void *)pt_phys, 0, sizeof(page_table_t));
        page_dir->tables[dir_idx] =
            pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else if (page_dir != current_page_dir && dir_idx < NUM_PAGE_TABLES) {
        // Entry points to a shared kernel page table — make a private copy
        // before modifying, to avoid corrupting the kernel's identity mapping.
        uint32_t kernel_pt = current_page_dir->tables[dir_idx] & ~0xFFF;
        uint32_t cur_pt = page_dir->tables[dir_idx] & ~0xFFF;
        if (cur_pt == kernel_pt) {
            uint32_t new_pt = pmm_alloc_frame();
            if (!new_pt) {
                printf(
                    "[paging] failed to allocate COW page table for dir %d\n",
                    dir_idx);
                return -1;
            }
            memcpy((void *)new_pt, (void *)kernel_pt, sizeof(page_table_t));
            page_dir->tables[dir_idx] =
                new_pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
    }

    // Get physical address of page table (mask off flags)
    page_table_t *pt = (page_table_t *)(page_dir->tables[dir_idx] & ~0xFFF);
    pt->pages[table_idx] = (physical_addr & ~0xFFF) | flags;

    // Invalidate TLB for this page
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
    return 0;
}

// Unmap a virtual page
void paging_unmap_page(page_directory_t *page_dir, uint32_t virtual_addr) {
    uint32_t dir_idx = virtual_addr >> 22;
    uint32_t table_idx = (virtual_addr >> 12) & 0x3FF;

    if (!(page_dir->tables[dir_idx] & PAGE_PRESENT))
        return;

    page_table_t *pt = (page_table_t *)(page_dir->tables[dir_idx] & ~0xFFF);
    pt->pages[table_idx] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

// Switch to a different address space
void paging_switch(page_directory_t *page_dir) {
    uint32_t phys = (uint32_t)page_dir; // Identity-mapped, virt == phys
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

// Destroy a per-process address space, freeing PMM frames
void paging_destroy_address_space(page_directory_t *page_dir) {
    if (!page_dir || page_dir == current_page_dir)
        return;

    // Free any private copies of kernel page tables (0..NUM_PAGE_TABLES-1).
    // Table 1 always gets a private copy; others get COW copies when modified.
    for (uint32_t i = 0; i < NUM_PAGE_TABLES; i++) {
        if (!(page_dir->tables[i] & PAGE_PRESENT))
            continue;
        uint32_t pt_phys = page_dir->tables[i] & ~0xFFF;
        uint32_t kernel_pt = current_page_dir->tables[i] & ~0xFFF;
        if (pt_phys == kernel_pt)
            continue; // Still shared, don't free

        // This is a private copy — free user-allocated frames in it
        page_table_t *pt = (page_table_t *)pt_phys;
        for (uint32_t j = 0; j < 1024; j++) {
            if (pt->pages[j] & PAGE_PRESENT) {
                uint32_t frame = pt->pages[j] & ~0xFFF;
                // Only free frames that came from PMM (not identity-mapped
                // kernel pages)
                uint32_t kernel_frame =
                    (i * 1024 + j) * 0x1000; // Expected identity-map addr
                if (frame != kernel_frame && frame >= PMM_START &&
                    frame < PMM_END) {
                    pmm_free_frame(frame);
                }
            }
        }
        pmm_free_frame(pt_phys);
    }

    // Free any additional page tables we allocated (beyond the shared kernel
    // ones) Skip VBE/BGA directory entries (shared from kernel, must not be
    // freed)
    for (uint32_t i = NUM_PAGE_TABLES; i < 1024; i++) {
        if (page_dir->tables[i] & PAGE_PRESENT) {
            // Check if this is a VBE shared entry — skip if so
            int is_vbe = 0;
            for (int v = 0; v < vbe_dir_count; v++) {
                if (vbe_dir_indices[v] == i) {
                    is_vbe = 1;
                    break;
                }
            }
            if (is_vbe)
                continue;

            uint32_t pt_phys = page_dir->tables[i] & ~0xFFF;
            // Free frames in this page table
            page_table_t *pt = (page_table_t *)pt_phys;
            for (uint32_t j = 0; j < 1024; j++) {
                if (pt->pages[j] & PAGE_PRESENT) {
                    uint32_t frame = pt->pages[j] & ~0xFFF;
                    if (frame >= PMM_START && frame < PMM_END) {
                        pmm_free_frame(frame);
                    }
                }
            }
            pmm_free_frame(pt_phys);
        }
    }

    // Free the page directory itself
    pmm_free_frame((uint32_t)page_dir);
}
