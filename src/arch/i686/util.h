#ifndef _UTIL_H
#define _UTIL_H

#include "../../lib.h"

void cause_div_exception(void);
int check_protected_mode(void);
void print_registers(void);
void print_stack(uint32_t entries);
void print_cpu_info(void);

extern void halt_and_catch_fire(void);

#endif
