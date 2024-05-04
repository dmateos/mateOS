#include "kernel.h"
#include "arch/i686/legacytty.h"

void outb(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void inb(uint16_t port, uint8_t *value) {
  asm volatile("inb %1, %0" : "=a"(*value) : "Nd"(port));
}

void kernel_main(void) {
  terminal_initialize();

  while (1) {
    terminal_writestring("Hello, kernel World!\n");
  }
}
