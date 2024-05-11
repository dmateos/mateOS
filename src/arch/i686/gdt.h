#ifndef _GDT_H
#define _GDT_H

#include "../../lib.h"

typedef struct gdt_entry {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_middle;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

extern void flush_gdt(gdt_ptr_t *gp_ptr);
void init_gdt(gdt_ptr_t *gp_ptr, gdt_entry_t *gdt);

#endif