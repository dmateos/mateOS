#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
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
#include "arch/i686/io.h"
#include "window.h"
#include "arch/i686/legacytty.h"
#include "net.h"
#include "arch/i686/mouse.h"
#include "version.h"

// External Rust functions
extern void rust_hello(void);
extern int rust_add(int a, int b);

static int cmdline_has_token(const char *cmdline, const char *token) {
  if (!cmdline || !token || !token[0]) return 0;
  size_t tlen = strlen(token);
  const char *p = cmdline;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') p++;
    size_t len = (size_t)(p - start);
    if (len == tlen && memcmp(start, token, tlen) == 0) return 1;
  }
  return 0;
}

void kernel_main(uint32_t multiboot_magic, multiboot_info_t *multiboot_info) {
  init_686();
  kprintf("[boot] mateOS %s (abi=%d, built=%s)\n",
          KERNEL_VERSION_FULL, KERNEL_VERSION_ABI, KERNEL_BUILD_DATE_UTC);
  kprintf("[boot] paging init ok\n");

  // Parse multiboot info (if provided by bootloader)
  printf("\n");
  multiboot_init(multiboot_magic, multiboot_info);
  const char *cmdline = multiboot_get_cmdline();
  if (cmdline_has_token(cmdline, "serial=1") || cmdline_has_token(cmdline, "autorun=cctest")) {
    serial_init();
    console_set_serial_mirror(1);
    kprintf("[boot] serial mirror enabled\n");
  }

  // Initialize ramfs from initrd module
  multiboot_module_t *initrd = multiboot_get_initrd();
  if (initrd) {
    ramfs_init((void *)initrd->mod_start,
               initrd->mod_end - initrd->mod_start);
  } else {
    ramfs_init(NULL, 0);
  }
  printf("\n");

  keyboard_init_interrupts();

  // Test Rust integration on boot
  printf("\n");
  rust_hello();
  printf("Rust test: 40 + 2 = %d\n\n", rust_add(40, 2));

  // Initialize physical memory manager
  pmm_init();
  kprintf("[boot] pmm init ok\n");

  // Scan PCI bus
  pci_init();
  kprintf("[boot] pci scan ok\n");

  // Initialize network (RTL8139 + minimal ARP/ICMP)
  net_init();
  kprintf("[boot] net init ok\n");

  // Initialize VFS and register ramfs
  vfs_init();
  kprintf("[boot] vfs init ok\n");
  vfs_register_fs(ramfs_get_ops());
  if (fat16_init() == 0) {
    vfs_register_fs(fat16_get_ops());
  }

  // Initialize task system
  task_init();
  kprintf("[boot] task init ok\n");

  // Initialize syscall handler
  syscall_init();
  kprintf("[boot] syscall init ok\n");

  // Initialize window manager subsystem
  window_init();
  kprintf("[boot] window init ok\n");

  // Initialize PS/2 mouse
  mouse_init();
  register_interrupt_handler(0x2C, mouse_irq_handler);
  pic_unmask_irq(12);

  // Boot message
  console_init();

  // Enable keyboard buffer for userland shell
  keyboard_buffer_init();
  keyboard_buffer_enable(1);

  const char *boot_prog = "shell.elf";
  if (cmdline_has_token(cmdline, "autorun=cctest")) {
    boot_prog = "cctest.elf";
    kprintf("[boot] autorun requested: %s\n", boot_prog);
  }

  // Auto-launch boot program — loaded directly, no kernel trampoline
  task_t *boot_task = task_create_user_elf(boot_prog, NULL, 0);
  if (boot_task) {
    task_enable();
  } else {
    printf("WARNING: %s not found in ramfs\n", boot_prog);
    printf("No boot program available. System halted.\n");
  }

  // Main loop - just halt and wait for interrupts
  while (1) {
    halt_and_catch_fire();
  }
}
