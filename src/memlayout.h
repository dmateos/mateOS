#ifndef _MEMLAYOUT_H
#define _MEMLAYOUT_H

#include "lib.h"

// Higher-half kernel: linked at VMA 0xC0200000, loaded at LMA 0x200000.
// Physical 0-1GB is mapped at 0xC0000000-0xFFFFFFFF (higher-half only).
// No identity map — user processes own VA 0x00400000-0xBFFFFFFF.
// All kernel physical dereferences use PHYS_TO_KVIRT().
// PMM supports up to 1GB RAM (limited by the 1GB higher-half VA window).
#define KERNEL_VIRTUAL_BASE 0xC0000000u
#define KVIRT_TO_PHYS(v) ((v) - KERNEL_VIRTUAL_BASE)
#define PHYS_TO_KVIRT(p) ((p) + KERNEL_VIRTUAL_BASE)

// Kernel heap lives in the higher-half mapping.
// Starts at 5MB to leave room for BSS (page tables, GDT, IDT, etc.)
#define KERNEL_HEAP_START 0xC0500000u
#define KERNEL_HEAP_END 0xC0700000u

#define USER_REGION_START 0x00400000u
#define USER_REGION_END 0xC0000000u

#define USER_STACK_TOP_PAGE_VADDR 0xBFFFF000u
#define USER_STACK_PAGES 16u
#define USER_STACK_BASE_VADDR                                                  \
    (USER_STACK_TOP_PAGE_VADDR - ((USER_STACK_PAGES - 1u) * 0x1000u))

// Guard page: one page below the stack, must remain unmapped to catch overflow
#define USER_STACK_GUARD_VADDR (USER_STACK_BASE_VADDR - 0x1000u)

#endif
