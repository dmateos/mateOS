#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/util.h"

void test_interrupt_handler(uint32_t number, uint32_t error_code) {
  printf("Interrupt number: %d\n", number);
}

void kernel_main(void) {
  init_686();
  register_interrupt_handler(0x21, test_interrupt_handler);

  while (1) {
    halt_and_catch_fire();
  }
}
