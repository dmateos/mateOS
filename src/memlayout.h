#ifndef _MEMLAYOUT_H
#define _MEMLAYOUT_H

#include "lib.h"

// Higher-half kernel: linked at VMA 0xC0200000, loaded at LMA 0x200000.
// Physical 0-32MB is mapped at 0xC0000000-0xC1FFFFFF (higher-half only).
// No identity map — user processes own VA 0x00400000-0xBFFFFFFF.
// All kernel physical dereferences use PHYS_TO_KVIRT().
#define KERNEL_VIRTUAL_BASE 0xC0000000u
#define KVIRT_TO_PHYS(v) ((v) - KERNEL_VIRTUAL_BASE)
#define PHYS_TO_KVIRT(p) ((p) + KERNEL_VIRTUAL_BASE)

// Kernel heap lives in the higher-half mapping
#define KERNEL_HEAP_START 0xC0400000u
#define KERNEL_HEAP_END 0xC0600000u

#define USER_REGION_START 0x00400000u
#define USER_REGION_END 0xC0000000u

#define USER_STACK_TOP_PAGE_VADDR 0xBFFFF000u
#define USER_STACK_PAGES 16u
#define USER_STACK_BASE_VADDR                                                  \
    (USER_STACK_TOP_PAGE_VADDR - ((USER_STACK_PAGES - 1u) * 0x1000u))

// Guard page: one page below the stack, must remain unmapped to catch overflow
#define USER_STACK_GUARD_VADDR (USER_STACK_BASE_VADDR - 0x1000u)

#endif
