#include "slot_table.h"

int slot_table_find_free_by_flag(void *items, uint32_t count, uint32_t stride,
                                 uint32_t flag_offset) {
    if (!items || stride == 0)
        return -1;

    uint8_t *base = (uint8_t *)items;
    for (uint32_t i = 0; i < count; i++) {
        int *flag = (int *)(base + (i * stride) + flag_offset);
        if (!*flag)
            return (int)i;
    }
    return -1;
}

void slot_table_set_flag_by_index(void *items, uint32_t stride,
                                  uint32_t flag_offset, uint32_t idx,
                                  int value) {
    if (!items || stride == 0)
        return;
    uint8_t *base = (uint8_t *)items;
    int *flag = (int *)(base + (idx * stride) + flag_offset);
    *flag = value;
}
