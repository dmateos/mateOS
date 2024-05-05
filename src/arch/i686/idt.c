#include "idt.h"

idt_entry_t idt_entries[256];
idt_ptr_t idt_ptr;

void init_idt() {
  idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
  idt_ptr.base = (uint32_t)&idt_entries;

  // memset(&idt_entries, 0, sizeof(idt_entry_t) * 256);
  // idt_load(&idt_ptr);
}