#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/timer.h"
#include "arch/i686/util.h"
#include "console.h"
#include "keyboard.h"
#include "liballoc/liballoc_1_1.h"
#include "multiboot.h"
#include "ramfs.h"
#include "task.h"
#include "syscall.h"

// Define to auto-run demo on boot (uncomment to enable)
// #define AUTO_USERTEST

// External Rust functions
extern void rust_hello(void);
extern int rust_add(int a, int b);

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

  // If in graphics mode, signal the gfx wait loop instead of console
  if (console_gfx_check_key()) {
    return;
  }

  // If user gfx key buffer is active, push to it instead of console
  if (keyboard_buffer_is_enabled()) {
    char c = keyboard_translate(scancode);
    if (c) {
      keyboard_buffer_push((uint8_t)c);
    }
    return;
  }

  // Process key press and send to console
  char c = keyboard_translate(scancode);
  if (c) {
    console_handle_key(c);
  }
}

void kernel_main(uint32_t multiboot_magic, multiboot_info_t *multiboot_info) {
  init_686();

  // Parse multiboot info (if provided by bootloader)
  printf("\n");
  multiboot_init(multiboot_magic, multiboot_info);

  // Initialize ramfs from initrd module
  multiboot_module_t *initrd = multiboot_get_initrd();
  if (initrd) {
    ramfs_init((void *)initrd->mod_start,
               initrd->mod_end - initrd->mod_start);
  } else {
    ramfs_init(NULL, 0);
  }
  printf("\n");

  kernel_interrupt_t test = {.register_interrupt_handler =
                                 register_interrupt_handler};

  test.register_interrupt_handler(0x21, test_interrupt_handler);

  // Test Rust integration on boot
  printf("\n");
  rust_hello();
  printf("Rust test: 40 + 2 = %d\n\n", rust_add(40, 2));

  // Test memory allocator on boot
  printf("Testing memory allocator...\n");
  void *ptr1 = kmalloc(64);
  if (ptr1) {
    printf("  kmalloc(64) = 0x%x - OK\n", (uint32_t)ptr1);
    // Write and read back
    char *str = (char *)ptr1;
    str[0] = 'A';
    str[1] = 'B';
    str[2] = 'C';
    str[3] = '\0';
    printf("  Write/read test: '%s' - %s\n", str,
           (str[0] == 'A' && str[1] == 'B') ? "OK" : "FAIL");
    kfree(ptr1);
    printf("  kfree() - OK\n");
  } else {
    printf("  kmalloc FAILED!\n");
  }
  printf("\n");

  // Initialize task system
  task_init();

  // Initialize syscall handler
  syscall_init();

  // Initialize console
  console_init();

#ifdef AUTO_USERTEST
  // Auto-test demo on boot
  printf("\n");
  console_execute_command("demo");
#endif

  // Main loop - just halt and wait for interrupts
  while (1) {
    halt_and_catch_fire();
  }
}
