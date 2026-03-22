/*
 * paging.c — x86_64 4-level page table implementation
 *
 * Implements the arch_aspace_* functions (via arch_impl.c wrappers) and
 * some x86_64-internal helpers.
 *
 * Virtual address layout:
 *   [63:48]  sign extension (canonical form — must match bit 47)
 *   [47:39]  PML4 index   (9 bits, 512 entries)
 *   [38:30]  PDPT index   (9 bits)
 *   [29:21]  PD index     (9 bits)
 *   [20:12]  PT index     (9 bits)
 *   [11:0]   page offset  (12 bits, 4KB pages)
 *
 * Physical memory: we keep a simple bump allocator for page-table frames.
 * Real PMM integration happens later; for now frames come from a statically
 * reserved pool so the x86_64 build can at least boot and run kernel tasks.
 *
 * Kernel higher-half: 0xC0000000 (same as i686 — fits in 32-bit range of the
 * 64-bit address space, so PML4 index = 0 and PDPT index = 3).
 */
#include "arch/x86_64/paging.h"
#include "memlayout.h"
#include "lib.h"
#include "proc/pmm.h"

/* ── Page-table frame allocator ────────────────────────────────────────── */

static uintptr_t pt_alloc_frame(void) {
    /* Use the kernel PMM once it's initialised.
     * A frame is 4096 bytes and must be physically aligned. */
    uintptr_t phys = (uintptr_t)(uint32_t)pmm_alloc_frame();
    if (!phys)
        return 0;
    /* Zero the frame so that all entries are "not present" */
    uint8_t *virt = (uint8_t *)PHYS_TO_KVIRT(phys);
    memset(virt, 0, 4096);
    return phys;
}

/* ── Kernel PML4 (shared across all processes) ─────────────────────────── */

static pml4_t kernel_pml4 __attribute__((aligned(4096)));
static int    kernel_pml4_init_done = 0;

/* pml4_t for the boot kernel mapping (the one set up in boot.S) — we keep a
 * C-level mirror so arch_aspace_kernel() can return it. */
pml4_t *paging64_get_kernel_pml4(void) {
    return &kernel_pml4;
}

/* ── Virtual-address decomposition ─────────────────────────────────────── */

#define VA_PML4(va)  (((va) >> 39) & 0x1FF)
#define VA_PDPT(va)  (((va) >> 30) & 0x1FF)
#define VA_PD(va)    (((va) >> 21) & 0x1FF)
#define VA_PT(va)    (((va) >> 12) & 0x1FF)

/* ── Walk/allocate a page-table entry ──────────────────────────────────── */

/*
 * Return a pointer to the PT entry for vaddr in the given PML4.
 * If create=1, allocates intermediate tables on demand.
 * Returns NULL if any allocation fails or if create=0 and the path is absent.
 */
static uint64_t *pt_entry_for(pml4_t *pml4, uintptr_t vaddr, int create) {
    uint64_t *pml4e = &pml4->entries[VA_PML4(vaddr)];
    pdpt_t *pdpt;

    if (!(*pml4e & PAGE_PRESENT)) {
        if (!create) return NULL;
        uintptr_t phys = pt_alloc_frame();
        if (!phys) return NULL;
        *pml4e = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        pdpt = (pdpt_t *)PHYS_TO_KVIRT(phys);
    } else {
        pdpt = (pdpt_t *)PHYS_TO_KVIRT(*pml4e & ~0xFFFULL);
    }

    uint64_t *pdpte = &pdpt->entries[VA_PDPT(vaddr)];
    pd_t *pd;

    if (!(*pdpte & PAGE_PRESENT)) {
        if (!create) return NULL;
        uintptr_t phys = pt_alloc_frame();
        if (!phys) return NULL;
        *pdpte = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        pd = (pd_t *)PHYS_TO_KVIRT(phys);
    } else {
        pd = (pd_t *)PHYS_TO_KVIRT(*pdpte & ~0xFFFULL);
    }

    uint64_t *pde = &pd->entries[VA_PD(vaddr)];
    pt_t *pt;

    if (!(*pde & PAGE_PRESENT)) {
        if (!create) return NULL;
        uintptr_t phys = pt_alloc_frame();
        if (!phys) return NULL;
        *pde = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        pt = (pt_t *)PHYS_TO_KVIRT(phys);
    } else {
        if (*pde & (1ULL << 7)) return NULL;  /* superpage — can't map 4KB here */
        pt = (pt_t *)PHYS_TO_KVIRT(*pde & ~0xFFFULL);
    }

    return &pt->entries[VA_PT(vaddr)];
}

/* ── Public paging functions (called from arch_impl.c) ─────────────────── */

/*
 * Map a 4KB page vaddr→paddr with the given PAGE_* flags.
 * Returns 0 on success, -1 on allocation failure.
 */
int paging64_map_page(pml4_t *pml4, uintptr_t vaddr, uintptr_t paddr,
                      uint64_t flags) {
    uint64_t *pte = pt_entry_for(pml4, vaddr, 1);
    if (!pte) return -1;
    *pte = (paddr & ~0xFFFULL) | flags;
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}

void paging64_unmap_page(pml4_t *pml4, uintptr_t vaddr) {
    uint64_t *pte = pt_entry_for(pml4, vaddr, 0);
    if (pte) {
        *pte = 0;
        __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    }
}

uintptr_t paging64_user_phys(pml4_t *pml4, uintptr_t vaddr) {
    uint64_t *pte = pt_entry_for(pml4, vaddr, 0);
    if (!pte) return 0;
    uint64_t entry = *pte;
    if (!(entry & PAGE_PRESENT)) return 0;
    if (!(entry & PAGE_USER))    return 0;
    return (uintptr_t)(entry & ~0xFFFULL);
}

/*
 * Create a new address space by allocating a fresh PML4 and sharing kernel
 * entries (higher-half mappings).  Returns NULL on OOM.
 */
pml4_t *paging64_create_address_space(void) {
    uintptr_t phys = pt_alloc_frame();
    if (!phys) return NULL;

    pml4_t *new_pml4 = (pml4_t *)PHYS_TO_KVIRT(phys);

    /* Copy kernel higher-half PML4 entries from the kernel PML4.
     * In our layout, the kernel lives at VA 0xC0000000 (PML4 index 0,
     * PDPT index 3).  We share PML4 entry 0 so kernel mappings are visible
     * to all processes. */
    memset(new_pml4, 0, sizeof(pml4_t));

    /* Share the kernel's PML4 entry 0 (which points to the PDPT that has
     * the higher-half entry for 0xC0000000) */
    if (kernel_pml4_init_done)
        new_pml4->entries[0] = kernel_pml4.entries[0];

    return new_pml4;
}

void paging64_switch(pml4_t *pml4) {
    uintptr_t phys = KVIRT_TO_PHYS((uintptr_t)pml4);
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}

void paging64_destroy_address_space(pml4_t *pml4) {
    if (!pml4) return;
    /* Walk and free all user-space page tables (PML4 entries 0 with user bit,
     * skipping the shared kernel higher-half entry). */
    for (int i = 0; i < 512; i++) {
        uint64_t pml4e = pml4->entries[i];
        if (!(pml4e & PAGE_PRESENT)) continue;
        /* Skip kernel-shared entries (they have no USER flag) */
        if (i == 0 && !(pml4e & PAGE_USER)) continue;

        pdpt_t *pdpt = (pdpt_t *)PHYS_TO_KVIRT(pml4e & ~0xFFFULL);
        for (int j = 0; j < 512; j++) {
            uint64_t pdpte = pdpt->entries[j];
            if (!(pdpte & PAGE_PRESENT)) continue;
            if (pdpte & (1ULL << 7)) continue;  /* 1GB superpage — skip */

            pd_t *pd = (pd_t *)PHYS_TO_KVIRT(pdpte & ~0xFFFULL);
            for (int k = 0; k < 512; k++) {
                uint64_t pde = pd->entries[k];
                if (!(pde & PAGE_PRESENT)) continue;
                if (pde & (1ULL << 7)) continue;  /* 2MB superpage — skip */

                /* Free the PT frame */
                pmm_free_frame((uint32_t)(pde & ~0xFFFULL));
            }
            pmm_free_frame((uint32_t)(pdpte & ~0xFFFULL));
        }
        pmm_free_frame((uint32_t)(pml4e & ~0xFFFULL));
    }
    /* Free the PML4 itself */
    pmm_free_frame((uint32_t)KVIRT_TO_PHYS((uintptr_t)pml4));
}

/*
 * Map a physically-contiguous MMIO region into the kernel address space.
 * Uses the kernel PML4 directly.
 */
void paging64_map_mmio(uintptr_t phys, uint32_t size) {
    uintptr_t vaddr = PHYS_TO_KVIRT(phys);
    for (uint32_t offset = 0; offset < size; offset += 4096) {
        paging64_map_page(&kernel_pml4,
                          vaddr + offset,
                          phys + offset,
                          PAGE_PRESENT | PAGE_WRITE);
    }
}

/*
 * init_paging: called from x86_64init.c after PMM is ready.
 * Builds the kernel PML4 by identity-mapping 0–8MB and mapping the
 * higher-half (0xC0000000+ = PA 0+).  The boot.S temporary tables are
 * replaced by this permanent setup.
 */
void init_paging(void) {
    memset(&kernel_pml4, 0, sizeof(kernel_pml4));

    /* Map first 8MB at both identity (VA=PA) and higher-half (VA=PA+KVBASE)
     * using 4KB pages so we can later unmap the identity range. */
    for (uintptr_t pa = 0; pa < 8 * 1024 * 1024; pa += 4096) {
        /* identity map */
        paging64_map_page(&kernel_pml4, pa, pa,
                          PAGE_PRESENT | PAGE_WRITE);
        /* higher-half map */
        paging64_map_page(&kernel_pml4, PHYS_TO_KVIRT(pa), pa,
                          PAGE_PRESENT | PAGE_WRITE);
    }

    kernel_pml4_init_done = 1;
    paging64_switch(&kernel_pml4);
    kprintf("[paging64] kernel PML4 active\n");
}

/* ── CR2/CR3 accessors (used by exception handler) ─────────────────────── */

uint64_t get_cr2(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

uint64_t get_cr3(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}
