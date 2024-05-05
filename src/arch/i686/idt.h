#ifndef _IDT_H
#define _IDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct idt_entry {
  uint16_t base_low;
  uint16_t selector;
  uint8_t zero;
  uint8_t flags;
  uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void init_idt();

#endif