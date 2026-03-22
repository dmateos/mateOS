#ifndef _ARCH_X86_64_TIMER_H
#define _ARCH_X86_64_TIMER_H

#include <stdint.h>

void     init_timer(uint32_t frequency);
uint32_t get_tick_count(void);
uint32_t get_uptime_seconds(void);

#endif
