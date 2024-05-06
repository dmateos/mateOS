#include "kernel.h"
#include "lib.h"

#include "arch/i686/gdt.h"
#include "arch/i686/idt.h"
#include "arch/i686/legacytty.h"
#include "arch/i686/util.h"

void kernel_main(void) {
  // temporary term output using VGA text mode
  init_term();

  printf("mateOS kernel started\n");

  // Global and Interrupt Descriptor Tables
  init_gdt();
  init_idt();

  printf("mateOS init done\n");
  print_registers();

  while (1) {
    asm volatile("hlt"); // and catch fire
  }
}
