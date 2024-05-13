#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/util.h"
#include "keyboard.h"

typedef struct {
  void (*register_interrupt_handler)(uint8_t, void (*h)(uint32_t, uint32_t));
} kernel_interrupt_t;

void test_interrupt_handler(uint32_t number, uint32_t error_code) {
  // check if keydown
  if (inb(IO_KB_DATA) & 0x80) {
    return;
  }
  char c = keyboard_translate(inb(IO_KB_DATA));
  printf("%c", c);
}

void kernel_main(void) {
  init_686();

  kernel_interrupt_t test = {.register_interrupt_handler =
                                 register_interrupt_handler};

  test.register_interrupt_handler(0x21, test_interrupt_handler);

  // register_interrupt_handler(0x21, test_interrupt_handler);

  while (1) {
    halt_and_catch_fire();
  }
}
