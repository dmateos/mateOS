#ifndef _MOUSE_H
#define _MOUSE_H

#include "lib.h"

typedef struct {
    int x, y;
    uint8_t buttons;  // bit0=left, bit1=right, bit2=middle
} mouse_state_t;

void mouse_init(void);
void mouse_set_bounds(int w, int h);
mouse_state_t mouse_get_state(void);
void mouse_irq_handler(uint32_t num, uint32_t err);

#endif
