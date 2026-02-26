#ifndef _UTIL_H
#define _UTIL_H

#include "lib.h"

void cause_div_exception(void);
int check_protected_mode(void);
void print_registers(void);
void print_stack(uint32_t entries);
void print_cpu_info(void);
typedef struct {
  char vendor[13];
  uint32_t max_leaf;
  uint32_t family;
  uint32_t model;
  uint32_t stepping;
  uint32_t feature_ecx;
  uint32_t feature_edx;
} cpu_info_t;
void cpu_get_info(cpu_info_t *out);

extern void halt_and_catch_fire(void);

#endif
