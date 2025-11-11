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

void test_interrupt_handler(uint32_t number __attribute__((unused)),
                            uint32_t error_code __attribute__((unused))) {
  uint8_t scancode = inb(IO_KB_DATA);

  // 0x80 bit indicates key release, so ignore it
  if (scancode & 0x80) {
    return;
  }

  // Process key press
  char c = keyboard_translate(scancode);
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
