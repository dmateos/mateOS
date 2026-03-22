/*
 * paging.h — x86_64 4-level paging types and function declarations
 */
#ifndef _ARCH_X86_64_PAGING_H
#define _ARCH_X86_64_PAGING_H

#include "lib.h"

/* Page flags (match i686 conventions for easy porting) */
#define PAGE_PRESENT  0x001ULL
#define PAGE_WRITE    0x002ULL
#define PAGE_USER     0x004ULL
#define PAGE_PS       0x080ULL  /* page size bit (2MB/1GB superpage) */
#define PAGE_NX       (1ULL << 63)

/* 4-level paging structures: each is a 512-entry table of 8-byte entries */
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pml4_t;
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pdpt_t;
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pd_t;
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pt_t;

/* Initialise kernel PML4 (called from x86_64init.c after PMM is ready) */
void init_paging(void);

/* Address space operations (called by arch_impl.c) */
pml4_t *paging64_create_address_space(void);
void    paging64_destroy_address_space(pml4_t *pml4);
void    paging64_switch(pml4_t *pml4);
pml4_t *paging64_get_kernel_pml4(void);

int       paging64_map_page(pml4_t *pml4, uintptr_t vaddr, uintptr_t paddr,
                            uint64_t flags);
void      paging64_unmap_page(pml4_t *pml4, uintptr_t vaddr);
uintptr_t paging64_user_phys(pml4_t *pml4, uintptr_t vaddr);
void      paging64_map_mmio(uintptr_t phys, uint32_t size);

/* CR2/CR3 accessors */
uint64_t get_cr2(void);
uint64_t get_cr3(void);

#endif /* _ARCH_X86_64_PAGING_H */
