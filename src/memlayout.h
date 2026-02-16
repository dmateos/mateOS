#ifndef _MEMLAYOUT_H
#define _MEMLAYOUT_H

#include "lib.h"

#define KERNEL_HEAP_START          0x00400000u
#define KERNEL_HEAP_END            0x00600000u

#define USER_REGION_START          0x00700000u
#define USER_REGION_END            0x00800000u

#define USER_STACK_TOP_PAGE_VADDR  0x007F0000u
#define USER_STACK_PAGES           16u
#define USER_STACK_BASE_VADDR      (USER_STACK_TOP_PAGE_VADDR - ((USER_STACK_PAGES - 1u) * 0x1000u))

#endif
