#include "kernel.h"
#include "lib.h"

#include "arch/i686/gdt.h"
#include "arch/i686/idt.h"
#include "arch/i686/legacytty.h"
#include "arch/i686/util.h"

extern void test_assembly();

void kernel_main(void) {
  // temporary term output using VGA text mode
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
  test_assembly();
  test_assembly();
  print_registers();

  while (1) {
    halt_and_catch_fire();
  }
}
