#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/util.h"

void kernel_main(void) {
  init_686();

  // cause_div_exception();

  while (1) {
    halt_and_catch_fire();
  }
}
