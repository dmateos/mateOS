#ifndef _LIBALLOC_HOOKS_H
#define _LIBALLOC_HOOKS_H

#include <stdint.h>

void liballoc_heap_info(uint32_t *start, uint32_t *end, uint32_t *current);

#endif
