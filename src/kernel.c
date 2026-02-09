#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/timer.h"
#include "arch/i686/util.h"
#include "arch/i686/paging.h"
#include "console.h"
#include "keyboard.h"
#include "liballoc/liballoc_1_1.h"
#include "multiboot.h"
#include "ramfs.h"
#include "task.h"
#include "syscall.h"

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

  // If keyboard buffer is active, push to it (shell reads via SYS_GETKEY)
  if (keyboard_buffer_is_enabled()) {
    char c = keyboard_translate(scancode);
    if (c) {
      keyboard_buffer_push((uint8_t)c);
    }
    return;
  }

  // Fallback: send to console (only during early boot)
  char c = keyboard_translate(scancode);
  if (c) {
    console_handle_key(c);
  }
}

// Shell launcher: trampoline that exec's shell.elf
static char shell_filename[] = "shell.elf";

static void shell_entry(void) {
  sys_exec(shell_filename);
  sys_exit(127);  // exec failed
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

  // Initialize task system
  task_init();

  // Initialize syscall handler
  syscall_init();

  // Boot message
  console_init();

  // Enable keyboard buffer for userland shell
  keyboard_buffer_init();
  keyboard_buffer_enable(1);

  // Mark shell_filename page as user-accessible
  paging_set_user((uint32_t)shell_filename & ~0xFFF);

  // Auto-launch shell.elf
  ramfs_file_t *shell_file = ramfs_lookup("shell.elf");
  if (shell_file) {
    task_t *t = task_create_user("shell", shell_entry);
    if (t) {
      task_enable();
    } else {
      printf("ERROR: Failed to create shell task\n");
    }
  } else {
    printf("WARNING: shell.elf not found in ramfs\n");
    printf("No shell available. System halted.\n");
  }

  // Main loop - just halt and wait for interrupts
  while (1) {
    halt_and_catch_fire();
  }
}
