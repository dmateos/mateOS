#include "686init.h"

#include "gdt.h"
#include "interrupts.h"
#include "legacytty.h"
#include "util.h"

void init_686(void) {
  init_term();

  printf("mateOS kernel started\n");
  if (!check_protected_mode()) {
    printf("Protected mode not enabled\n");
    return;
  }

  // Global and Interrupt Descriptor Tables
  init_gdt();
  init_idt();

  printf("mateOS init done\n");
  print_registers();
}