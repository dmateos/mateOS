#include "kernel.h"
#include "lib.h"

#include "arch/i686/686init.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/timer.h"
#include "arch/i686/util.h"
#include "arch/i686/paging.h"
#include "io/console.h"
#include "io/keyboard.h"
#include "liballoc/liballoc_1_1.h"
#include "boot/multiboot.h"
#include "fs/ramfs.h"
#include "fs/vfs.h"
#include "fs/fat16.h"
#include "proc/task.h"
#include "syscall.h"
#include "proc/pmm.h"
#include "memlayout.h"
#include "arch/i686/pci.h"
#include "arch/i686/io.h"
#include "io/window.h"
#include "arch/i686/legacytty.h"
#include "net/net.h"
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

static int cmdline_get_value(const char *cmdline, const char *key,
                             char *out, size_t out_cap) {
  if (!cmdline || !key || !key[0] || !out || out_cap < 2) return 0;
  size_t klen = strlen(key);
  const char *p = cmdline;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') p++;
    size_t len = (size_t)(p - start);
    if (len > klen + 1 && start[klen] == '=' && memcmp(start, key, klen) == 0) {
      size_t vlen = len - (klen + 1);
      if (vlen >= out_cap) vlen = out_cap - 1;
      memcpy(out, start + klen + 1, vlen);
      out[vlen] = 0;
      return 1;
    }
  }
  return 0;
}

static int str_ends_with(const char *s, const char *sfx) {
  if (!s || !sfx) return 0;
  size_t ls = strlen(s);
  size_t lx = strlen(sfx);
  if (lx > ls) return 0;
  return memcmp(s + (ls - lx), sfx, lx) == 0;
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
  if (cmdline_has_token(cmdline, "serial=1") || cmdline_get_value(cmdline, "autorun", (char[2]){0}, 2)) {
    serial_init();
    console_set_serial_mirror(1);
    kprintf("[boot] serial mirror enabled\n");
  }

  // Initialize physical memory manager early (needed to relocate initrd)
  pmm_init();
  kprintf("[boot] pmm init ok\n");

  // Initialize ramfs from initrd module.
  // The initrd may overlap the kernel heap (0x400000-0x600000).  If so,
  // copy it into PMM frames first so ramfs pointers stay valid.
  multiboot_module_t *initrd = multiboot_get_initrd();
  if (initrd) {
    uint32_t initrd_start = initrd->mod_start;
    uint32_t initrd_size = initrd->mod_end - initrd->mod_start;

    // Reserve original initrd region in PMM so it isn't allocated
    pmm_reserve_region(initrd_start, initrd_size);
    kprintf("[boot] pmm reserved initrd: 0x%x-0x%x\n",
            initrd_start, initrd_start + initrd_size);

    if (initrd_start + initrd_size > KERNEL_HEAP_START) {
      // Initrd overlaps heap — relocate to PMM frames
      uint32_t nframes = (initrd_size + 0xFFF) / 0x1000;
      uint32_t copy_base = pmm_alloc_frames(nframes);
      if (copy_base) {
        memcpy((void *)copy_base, (void *)initrd_start, initrd_size);
        kprintf("[boot] initrd relocated: 0x%x -> 0x%x (%d bytes, %d frames)\n",
                initrd_start, copy_base, initrd_size, nframes);
        ramfs_init((void *)copy_base, initrd_size);
      } else {
        kprintf("[boot] WARNING: failed to allocate %d frames for initrd copy\n", nframes);
        ramfs_init((void *)initrd_start, initrd_size);
      }
    } else {
      ramfs_init((void *)initrd_start, initrd_size);
    }
  } else {
    ramfs_init(NULL, 0);
  }
  printf("\n");

  keyboard_init_interrupts();

  // Test Rust integration on boot
  printf("\n");
  rust_hello();
  printf("Rust test: 40 + 2 = %d\n\n", rust_add(40, 2));

  // NOTE: pmm_init() was already called above (line 94) before initrd
  // relocation.  A second pmm_init() here would wipe the bitmap and
  // free the frames that now hold the relocated initrd data, causing
  // "invalid ELF" corruption when those frames get reused.
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

  const char *boot_prog = "init.elf";
  char autorun_name[64];
  char autorun_prog[72];
  if (cmdline_get_value(cmdline, "autorun", autorun_name, sizeof(autorun_name))) {
    memset(autorun_prog, 0, sizeof(autorun_prog));
    size_t n = strlen(autorun_name);
    if (n >= sizeof(autorun_prog) - 1) n = sizeof(autorun_prog) - 1;
    memcpy(autorun_prog, autorun_name, n);
    autorun_prog[n] = 0;
    if (!str_ends_with(autorun_prog, ".elf") && n + 4 < sizeof(autorun_prog)) {
      memcpy(autorun_prog + n, ".elf", 5);
    }
    boot_prog = autorun_prog;
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
