#ifndef _ARCH_X86_64_PAGING_H
#define _ARCH_X86_64_PAGING_H

#include "lib.h"

/* 4-level paging: PML4 → PDPT → PD → PT, each 512 × 8-byte entries */
#define PAGE_PRESENT 0x1ULL
#define PAGE_WRITE   0x2ULL
#define PAGE_USER    0x4ULL
#define PAGE_NX      (1ULL << 63)

typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pml4_t;
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pdpt_t;
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pd_t;
typedef struct { uint64_t entries[512]; } __attribute__((aligned(4096))) pt_t;

/* Boot-time paging setup: identity-maps low memory, maps kernel higher-half */
void init_paging(void);

uint64_t get_cr2(void);
uint64_t get_cr3(void);

#endif
