#ifndef _UTIL_H
#define _UTIL_H

int check_protected_mode(void);
void print_registers(void);
void print_stack(int entries);

extern void halt_and_catch_fire();

#endif