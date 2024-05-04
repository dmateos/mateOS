#include "kernel.h"
#include "arch/i686/legacytty.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__linux__)
#error                                                                         \
    "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

#if !defined(__i386__)
#error "i386 required"
#endif

void kernel_main(void) {
  terminal_initialize();

  while (1) {
    terminal_writestring("Hello, kernel World!\n");
  }
}
