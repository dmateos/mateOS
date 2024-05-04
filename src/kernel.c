#include "kernel.h"
#include "arch/i686/legacytty.h"

void kernel_main(void) {
  terminal_initialize();

  while (1) {
    terminal_writestring("Hello, kernel World!\n");
  }
}
