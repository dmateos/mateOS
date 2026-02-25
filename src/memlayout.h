#ifndef _MEMLAYOUT_H
#define _MEMLAYOUT_H

#include "lib.h"

#define KERNEL_HEAP_START          0x00400000u
#define KERNEL_HEAP_END            0x00600000u

#define USER_REGION_START          0x00700000u
#define USER_REGION_END            0x00C00000u

#define USER_STACK_TOP_PAGE_VADDR  0x00BFF000u
#define USER_STACK_PAGES           16u
#define USER_STACK_BASE_VADDR      (USER_STACK_TOP_PAGE_VADDR - ((USER_STACK_PAGES - 1u) * 0x1000u))

// Guard page: one page below the stack, must remain unmapped to catch overflow
#define USER_STACK_GUARD_VADDR     (USER_STACK_BASE_VADDR - 0x1000u)

#endif
