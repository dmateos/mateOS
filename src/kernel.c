#include "kernel.h"
#include "arch/i686/gdt.h"
#include "arch/i686/idt.h"
#include "arch/i686/legacytty.h"
#include "lib.h"

void kernel_main(void) {
  init_term();

  // General and Interrupt Descriptor Tables
  init_gdt();
  init_idt();

  printf("dmOS kernel started\n");
  while (1) {
    // printf("dmOS kernel running %d\n", x++);
  }
}
