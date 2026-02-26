#ifndef _SLOT_TABLE_H
#define _SLOT_TABLE_H

#include "lib.h"

int slot_table_find_free_by_flag(void *items, uint32_t count,
                                 uint32_t stride, uint32_t flag_offset);
void slot_table_set_flag_by_index(void *items, uint32_t stride,
                                  uint32_t flag_offset, uint32_t idx, int value);

#endif
