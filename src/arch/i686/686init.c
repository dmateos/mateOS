#include "686init.h"

#include "gdt.h"
#include "interrupts.h"
#include "legacytty.h"
#include "util.h"

gdt_entry_t gdt[3];
gdt_ptr_t gp_ptr;

idt_entry_t idt_entries[256];
idt_ptr_t idt_ptr;

void init_686(void) {
  init_term();

  printf("mateOS kernel started\n");
  if (!check_protected_mode()) {
    printf("Protected mode not enabled\n");
    return;
  }

  // Global and Interrupt Descriptor Tables
  init_gdt(&gp_ptr, gdt);
  init_idt(&idt_ptr, idt_entries);

  printf("mateOS init done\n");
  print_registers();
}