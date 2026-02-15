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
#include "vfs.h"
#include "fat16.h"
#include "task.h"
#include "syscall.h"
#include "pmm.h"
#include "arch/i686/pci.h"
#include "window.h"
#include "arch/i686/legacytty.h"
#include "net.h"
#include "arch/i686/mouse.h"

// External Rust functions
extern void rust_hello(void);
extern int rust_add(int a, int b);


// Track PS/2 extended scancode prefix (0xE0)
static int kb_extended = 0;

void test_interrupt_handler(uint32_t number __attribute__((unused)),
                            uint32_t error_code __attribute__((unused))) {
  uint8_t scancode = inb(IO_KB_DATA);

  // Handle extended scancode prefix
  if (scancode == 0xE0) {
    kb_extended = 1;
    return;
  }

  if (kb_extended) {
    kb_extended = 0;
    // Only handle key press, not release (0x80 bit)
    if (!(scancode & 0x80)) {
      if (scancode == 0x49) {       // Page Up
        terminal_scroll_up();
        return;
      } else if (scancode == 0x51) { // Page Down
        terminal_scroll_down();
        return;
      } else if (keyboard_buffer_is_enabled()) {
        uint8_t key = 0;
        // Arrow keys (set 1, E0-prefixed)
        if (scancode == 0x4B) key = KEY_LEFT;
        else if (scancode == 0x4D) key = KEY_RIGHT;
        else if (scancode == 0x48) key = KEY_UP;
        else if (scancode == 0x50) key = KEY_DOWN;
        if (key) {
          keyboard_buffer_push(key);
          return;
        }
      }
    }
    // Other extended keys: ignore
    return;
  }

  // Pass all scancodes (including releases) to translate for shift tracking
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

  register_interrupt_handler(0x21, test_interrupt_handler);

  // Test Rust integration on boot
  printf("\n");
  rust_hello();
  printf("Rust test: 40 + 2 = %d\n\n", rust_add(40, 2));

  // Initialize physical memory manager
  pmm_init();

  // Scan PCI bus
  pci_init();

  // Initialize network (RTL8139 + minimal ARP/ICMP)
  net_init();

  // Initialize VFS and register ramfs
  vfs_init();
  vfs_register_fs(ramfs_get_ops());
  if (fat16_init() == 0) {
    vfs_register_fs(fat16_get_ops());
  }

  // Initialize task system
  task_init();

  // Initialize syscall handler
  syscall_init();

  // Initialize window manager subsystem
  window_init();

  // Initialize PS/2 mouse
  mouse_init();
  register_interrupt_handler(0x2C, mouse_irq_handler);
  pic_unmask_irq(12);

  // Boot message
  console_init();

  // Enable keyboard buffer for userland shell
  keyboard_buffer_init();
  keyboard_buffer_enable(1);

  // Auto-launch shell.elf — loaded directly, no kernel trampoline
  task_t *shell_task = task_create_user_elf("shell.elf", NULL, 0);
  if (shell_task) {
    task_enable();
  } else {
    printf("WARNING: shell.elf not found in ramfs\n");
    printf("No shell available. System halted.\n");
  }

  // Main loop - just halt and wait for interrupts
  while (1) {
    halt_and_catch_fire();
  }
}
