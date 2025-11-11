#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/timer.h"
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

  printf("\nSystem running. Press keys to test keyboard input.\n");
  printf("System will display uptime every second.\n\n");

  uint32_t last_second = 0;

  while (1) {
    uint32_t current_second = get_uptime_seconds();

    // Display uptime every second
    if (current_second != last_second) {
      last_second = current_second;
      printf("Uptime: %d seconds (ticks: %d)\n", current_second,
             get_tick_count());
    }

    halt_and_catch_fire();
  }
}
