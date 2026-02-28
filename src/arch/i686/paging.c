#include "paging.h"
#include "lib.h"
#include "memlayout.h"
#include "proc/pmm.h"

// Assembly functions to manipulate control registers
extern void enable_paging(uint32_t page_directory_physical);

// Number of page tables we use (each covers 4MB, 256 = 1GB)
// Covers the full higher-half range: PDE 768-1023 (0xC0000000-0xFFFFFFFF)
#define NUM_PAGE_TABLES 256
// Store pointers for later modification
static page_directory_t *current_page_dir = NULL;
static page_table_t *current_page_tables = NULL;

// VBE framebuffer page directory entries to propagate to child processes
#define VBE_MAX_DIR_ENTRIES 4
static uint32_t vbe_dir_indices[VBE_MAX_DIR_ENTRIES];
static int vbe_dir_count = 0;

// Higher-half page directory entry offset (0xC0000000 >> 22 = 768)
#define HIGHER_HALF_PDE_START 768

void init_paging(page_directory_t *page_dir, page_table_t *page_tables) {
    printf("Paging initialization starting\n");

    // Store pointers for later modification
    current_page_dir = page_dir;
    current_page_tables = page_tables;

    // page_dir and page_tables are at higher-half VMA (0xC0xxxxxx) since
    // they live in BSS.  We need physical addresses for PDE entries.
    uint32_t pd_phys = KVIRT_TO_PHYS((uint32_t)page_dir);

    // Verify alignment - page directory must be 4KB aligned
    if (pd_phys & 0xFFF) {
        printf("ERROR: Page directory not 4KB aligned (0x%x)\n", pd_phys);
        return;
    }

    // Clear page directory
    memset(page_dir, 0, sizeof(page_directory_t));

    // Set up page tables for higher-half mapping only.
    // Higher-half: entries 768..775 -> phys 0-32MB at VA 0xC0000000-0xC1FFFFFF
    // No identity map — user processes own VA 0-0xBFFFFFFF.
    for (uint32_t t = 0; t < NUM_PAGE_TABLES; t++) {
        page_table_t *pt = &page_tables[t];
        uint32_t pt_phys = KVIRT_TO_PHYS((uint32_t)pt);

        if (pt_phys & 0xFFF) {
            printf("ERROR: Page table %d not 4KB aligned (0x%x)\n", t, pt_phys);
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

        // PAGE_USER on the PDE is harmless: actual user access requires
        // PAGE_USER on the PTE too (which kernel PTEs don't have).
        // Setting it here lets paging_map_vbe() identity-map the VBE/BGA
        // framebuffer into these PDE slots without needing to update PDE flags.
        uint32_t pde_entry = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

        // Higher-half map only: entry 768+t (VA 0xC0000000 + t*4MB)
        // No identity map — user processes own VA 0-0xBFFFFFFF.
        page_dir->tables[HIGHER_HALF_PDE_START + t] = pde_entry;
    }

    printf("Higher-half mapped first %d MB at 0xC0000000 (no identity map)\n",
           NUM_PAGE_TABLES * 4);
    printf("[paging-map] user region: 0x%x-0x%x, stack: 0x%x-0x%x\n",
           USER_REGION_START, USER_REGION_END - 1,
           USER_STACK_BASE_VADDR, USER_STACK_TOP_PAGE_VADDR + 0x0FFF);
    printf("Page directory at phys 0x%x\n", pd_phys);
    printf("Enabling paging...\n");

    // Enable paging by loading CR3 with physical address and setting CR0.PG
    enable_paging(pd_phys);

    printf("Paging enabled successfully!\n");
    printf("CR3 = 0x%x\n", get_cr3());
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

// Create a new address space for a user process.
// Only copies higher-half kernel entries (768+) and VBE entries.
// User page tables (entries 0-767) are allocated on demand by
// paging_map_page().
page_directory_t *paging_create_address_space(void) {
    // Allocate a page-aligned page directory from PMM
    uint32_t pd_phys = pmm_alloc_frame();
    if (!pd_phys) {
        printf("[paging] failed to allocate page directory\n");
        return NULL;
    }

    page_directory_t *new_dir = (page_directory_t *)PHYS_TO_KVIRT(pd_phys);
    memset(new_dir, 0, sizeof(page_directory_t));

    // Copy higher-half kernel page directory entries (shared kernel page tables)
    for (uint32_t i = 0; i < NUM_PAGE_TABLES; i++) {
        new_dir->tables[HIGHER_HALF_PDE_START + i] =
            current_page_dir->tables[HIGHER_HALF_PDE_START + i];
    }

    // Copy VBE/Mode13h framebuffer page directory entries (if any)
    for (int i = 0; i < vbe_dir_count; i++) {
        uint32_t idx = vbe_dir_indices[i];
        new_dir->tables[idx] = current_page_dir->tables[idx];
    }

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
        memset((void *)PHYS_TO_KVIRT(pt_phys), 0, sizeof(page_table_t));
        page_dir->tables[dir_idx] =
            pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    // Get physical address of page table (mask off flags), convert to virtual
    page_table_t *pt =
        (page_table_t *)PHYS_TO_KVIRT(page_dir->tables[dir_idx] & ~0xFFF);
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

    page_table_t *pt =
        (page_table_t *)PHYS_TO_KVIRT(page_dir->tables[dir_idx] & ~0xFFF);
    pt->pages[table_idx] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

// Switch to a different address space.
// page_dir is always a virtual (higher-half) pointer — convert to physical
// for CR3.
void paging_switch(page_directory_t *page_dir) {
    uint32_t phys = KVIRT_TO_PHYS((uint32_t)page_dir);
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

// Destroy a per-process address space, freeing PMM frames.
// page_dir is a virtual (higher-half) pointer.
void paging_destroy_address_space(page_directory_t *page_dir) {
    if (!page_dir || page_dir == current_page_dir)
        return;

    // Free all user page tables (entries 0-767). These are all per-process.
    // Skip higher-half entries (768+) — shared kernel page tables.
    // Skip VBE entries — shared from kernel.
    for (uint32_t i = 0; i < HIGHER_HALF_PDE_START; i++) {
        if (!(page_dir->tables[i] & PAGE_PRESENT))
            continue;

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
        page_table_t *pt = (page_table_t *)PHYS_TO_KVIRT(pt_phys);
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

    // Free the page directory itself
    pmm_free_frame(KVIRT_TO_PHYS((uint32_t)page_dir));
}
