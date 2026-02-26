#ifndef _TIMER_H
#define _TIMER_H

#include "lib.h"

void init_timer(uint32_t frequency);
uint32_t get_tick_count(void);
uint32_t get_uptime_seconds(void);
void timer_handler(uint32_t irq, uint32_t error_code);

#endif
