#include "syscall.h"
#include "elf.h"
#include "lib.h"
#include "ramfs.h"
#include "vfs.h"
#include "task.h"
#include "keyboard.h"
#include "arch/i686/paging.h"
#include "arch/i686/vga.h"
#include "arch/i686/timer.h"
#include "arch/i686/cpu.h"
#include "liballoc/liballoc_1_1.h"
#include "pmm.h"
#include "window.h"
#include "net.h"
#include "arch/i686/mouse.h"

// Track whether a user program is in graphics mode
static int user_gfx_active = 0;
static int user_gfx_bga = 0;         // 1 if using BGA, 0 if Mode 13h
static uint32_t bga_fb_addr = 0;     // Physical/virtual address of BGA LFB
static uint32_t bga_width = 0;
static uint32_t bga_height = 0;
static uint32_t gfx_owner_pid = 0;   // Task ID that owns graphics mode

// Forward declaration for interrupt registration
extern void isr128(void);

// iret frame layout - what iret pops from the stack
typedef struct {
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t esp;    // Only present for ring transitions (user->kernel)
  uint32_t ss;     // Only present for ring transitions
} __attribute__((packed)) iret_frame_t;

// Write to console or window text buffer (if stdout redirected)
static int sys_do_write(int fd, const char *buf, size_t len) {
  if (buf == NULL || len == 0) {
    return -1;
  }

  // If fd=1 and task has stdout redirected to a window, append text there
  task_t *current = task_current();
  if (fd == 1 && current && current->stdout_wid >= 0) {
    return window_append_text(current->stdout_wid, buf, (int)len);
  }

  // Default: write to kernel console
  for (size_t i = 0; i < len; i++) {
    printf("%c", buf[i]);
  }

  return (int)len;
}

// Exit current task
static void sys_do_exit(int code) {
  // Only tear down graphics if the exiting task owns it
  task_t *current = task_current();
  if (user_gfx_active && current && current->id == gfx_owner_pid) {
    keyboard_buffer_enable(0);
    if (user_gfx_bga) {
      vga_exit_bga_mode();
    } else {
      vga_enter_text_mode();
    }
    user_gfx_active = 0;
    user_gfx_bga = 0;
    gfx_owner_pid = 0;
  }
  task_exit_with_code(code);
  // Should never return
}

// Yield to scheduler
static void sys_do_yield(void) {
  task_yield();
}

// Sleep current task for at least ms milliseconds.
static int sys_do_sleepms(uint32_t ms) {
  uint32_t start = get_tick_count();
  uint32_t ticks = (ms + 9) / 10;  // 100Hz timer => 10ms ticks
  if (ticks == 0) ticks = 1;
  while ((get_tick_count() - start) < ticks) {
    task_yield();
  }
  return 0;
}

// Load ELF segments into a page directory. Returns entry point, or 0 on error.
// If stack_phys_out is non-NULL, stores the physical address of the user stack page.
uint32_t load_elf_into(struct page_directory *page_dir, const char *filename,
                       uint32_t *stack_phys_out) {
  void *data = NULL;
  uint32_t size = 0;
  if (vfs_read_file(filename, &data, &size) < 0) {
    printf("[exec] file not found: %s\n", filename);
    return 0;
  }

  elf32_ehdr_t *elf = (elf32_ehdr_t *)data;
  if (!elf_validate(elf)) {
    printf("[exec] invalid ELF: %s\n", filename);
    kfree(data);
    return 0;
  }

  // Load program segments into per-process physical frames
  elf32_phdr_t *phdr = (elf32_phdr_t *)((uint8_t *)elf + elf->e_phoff);

  for (int i = 0; i < elf->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    uint32_t vaddr = phdr[i].p_vaddr;
    uint32_t memsz = phdr[i].p_memsz;
    uint32_t filesz = phdr[i].p_filesz;
    uint32_t offset = phdr[i].p_offset;

    uint8_t *src = (uint8_t *)elf + offset;

    uint32_t seg_start = vaddr & ~0xFFF;
    uint32_t seg_end = (vaddr + memsz + 0xFFF) & ~0xFFF;

    for (uint32_t page_vaddr = seg_start; page_vaddr < seg_end; page_vaddr += 0x1000) {
      uint32_t phys = pmm_alloc_frame();
      if (!phys) {
        printf("[exec] out of physical frames\n");
        kfree(data);
        return 0;
      }

      memset((void *)phys, 0, 0x1000);

      uint32_t copy_start = (page_vaddr > vaddr) ? page_vaddr : vaddr;
      uint32_t copy_end = (page_vaddr + 0x1000 < vaddr + filesz)
                            ? page_vaddr + 0x1000 : vaddr + filesz;

      if (copy_start < copy_end) {
        uint32_t dst_offset = copy_start - page_vaddr;
        uint32_t src_offset = copy_start - vaddr;
        memcpy((void *)(phys + dst_offset), src + src_offset, copy_end - copy_start);
      }

      paging_map_page(page_dir, page_vaddr, phys,
                      PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
  }

  // Allocate user stack
  uint32_t stack_phys = pmm_alloc_frame();
  if (!stack_phys) {
    printf("[exec] failed to allocate stack frame\n");
    kfree(data);
    return 0;
  }
  memset((void *)stack_phys, 0, 0x1000);

  paging_map_page(page_dir, 0x7F0000, stack_phys,
                  PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

  if (stack_phys_out) {
    *stack_phys_out = stack_phys;
  }

  uint32_t entry = elf->e_entry;
  kfree(data);
  return entry;
}

// Execute ELF binary from ramfs - replaces current process
static int sys_do_exec(const char *filename, iret_frame_t *frame) {
  if (!filename) return -1;

  task_t *current = task_current();
  if (!current || current->is_kernel || !current->page_dir) {
    return -1;
  }

  uint32_t entry = load_elf_into(current->page_dir, filename, NULL);
  if (!entry) return -1;

  // Flush TLB
  paging_switch(current->page_dir);

  // Modify the iret frame to jump to ELF entry with new stack
  frame->eip    = entry;
  frame->cs     = 0x1B;           // User code segment (RPL=3)
  frame->eflags = 0x202;          // IF=1, reserved bit 1
  frame->esp    = 0x7F0000 + 0x1000;  // Top of stack page
  frame->ss     = 0x23;           // User data segment (RPL=3)

  return 0;
}

// Enter graphics mode — try BGA (Bochs VGA) for 1024x768, else Mode 13h
static uint32_t sys_do_gfx_init(void) {
  if (user_gfx_active) {
    return user_gfx_bga ? bga_fb_addr : 0xA0000;
  }

  // Try BGA mode (QEMU -vga std)
  if (vga_bga_available()) {
    uint32_t lfb = vga_enter_bga_mode(1024, 768, 8);
    if (lfb) {
      uint32_t fb_size = 1024 * 768;  // 8bpp = 1 byte per pixel

      // Map LFB pages in kernel page directory (for propagation to new processes)
      paging_map_vbe(lfb, fb_size);

      // Also map into calling process's page directory
      task_t *current = task_current();
      if (current && current->page_dir) {
        uint32_t start = lfb & ~0xFFF;
        uint32_t end = (lfb + fb_size + 0xFFF) & ~0xFFF;
        for (uint32_t addr = start; addr < end; addr += 0x1000) {
          paging_map_page(current->page_dir, addr, addr,
                          PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        }
        // Flush TLB
        paging_switch(current->page_dir);
      }

      bga_fb_addr = lfb;
      bga_width = 1024;
      bga_height = 768;

      // Set up palette via DAC ports
      vga_init_palette();

      user_gfx_bga = 1;
      keyboard_buffer_init();
      keyboard_buffer_enable(1);
      user_gfx_active = 1;
      gfx_owner_pid = current ? current->id : 0;
      mouse_set_bounds((int)bga_width, (int)bga_height);

      return bga_fb_addr;
    }
  }

  // Fallback: Mode 13h
  vga_enter_mode13h();

  for (uint32_t addr = 0xA0000; addr < 0xB0000; addr += 0x1000) {
    paging_set_user(addr);
  }

  keyboard_buffer_init();
  keyboard_buffer_enable(1);
  user_gfx_active = 1;
  user_gfx_bga = 0;
  {
    task_t *cur = task_current();
    gfx_owner_pid = cur ? cur->id : 0;
  }
  mouse_set_bounds(320, 200);

  return 0xA0000;
}

// Return to text mode — only the gfx owner can do this
static void sys_do_gfx_exit(void) {
  if (!user_gfx_active) return;

  task_t *current = task_current();
  if (!current || current->id != gfx_owner_pid) return;

  keyboard_buffer_enable(0);
  if (user_gfx_bga) {
    vga_exit_bga_mode();
  } else {
    vga_enter_text_mode();
  }
  user_gfx_active = 0;
  user_gfx_bga = 0;
  gfx_owner_pid = 0;
}

// Return screen dimensions: (width << 16) | height
static uint32_t sys_do_gfx_info(void) {
  if (user_gfx_bga && bga_width && bga_height) {
    return (bga_width << 16) | bga_height;
  }
  // Fallback: Mode 13h dimensions
  return (320 << 16) | 200;
}

// Read key from buffer (non-blocking)
static uint32_t sys_do_getkey(uint32_t flags __attribute__((unused))) {
  return (uint32_t)keyboard_buffer_pop();
}

// Spawn: create a child process from an ELF in ramfs.
// If argv/argc are provided (argv != NULL, argc > 0), they are placed on
// the child's stack. Otherwise defaults to argv={filename}, argc=1.
// argv strings must be in the calling process's address space — they are
// copied into kernel buffers before the child's address space is created.
static int sys_do_spawn(const char *filename, const char **argv, int argc) {
  if (!filename) return -1;

  // Copy argv strings into kernel buffers (they're in parent's address space
  // which will not be accessible after we switch to the child's page dir).
  const char *kargv[16];
  char kargbuf[512];  // flat buffer for all arg strings
  int kargc = 0;

  if (argv && argc > 0) {
    if (argc > 16) argc = 16;
    uint32_t off = 0;
    for (int i = 0; i < argc; i++) {
      if (!argv[i]) break;
      size_t slen = strlen(argv[i]) + 1;
      if (off + slen > sizeof(kargbuf)) break;
      memcpy(kargbuf + off, argv[i], slen);
      kargv[i] = kargbuf + off;
      off += slen;
      kargc++;
    }
  }

  task_t *parent = task_current();
  task_t *t;
  if (kargc > 0) {
    t = task_create_user_elf(filename, kargv, kargc);
  } else {
    t = task_create_user_elf(filename, NULL, 0);
  }
  if (!t) return -1;

  // Inherit parent's stdout redirection
  if (parent && parent->stdout_wid >= 0) {
    t->stdout_wid = parent->stdout_wid;
  }

  if (!task_is_enabled()) {
    task_enable();
  }

  return (int)t->id;
}

// Detach: mark current task as detached from parent's wait
static int sys_do_detach(void) {
  task_t *current = task_current();
  if (!current) return -1;
  current->detached = 1;
  // Wake up any task waiting for us
  for (int i = 0; i < MAX_TASKS; i++) {
    task_t *t = task_get_by_index(i);
    if (t && t->state == TASK_BLOCKED && t->waiting_for == current->id) {
      t->state = TASK_READY;
      t->waiting_for = 0;
    }
  }
  return 0;
}

// Wait: block until a child task exits, return its exit code
static int sys_do_wait(uint32_t task_id) {
  task_t *child = task_get_by_id(task_id);
  if (!child) return -1;

  if (child->detached) return -3;  // Child has detached

  if (child->state == TASK_TERMINATED) {
    return child->exit_code;
  }

  // Block current task until child exits
  task_t *current = task_current();
  current->waiting_for = task_id;
  current->state = TASK_BLOCKED;
  task_yield();

  current->waiting_for = 0;

  // Check if we were woken because child detached
  if (child->detached) return -3;

  return child->exit_code;
}

// Non-blocking wait: returns -1 if child still running, else exit code
static int sys_do_wait_nb(uint32_t task_id) {
  task_t *child = task_get_by_id(task_id);
  if (!child) return -2;  // No such task

  if (child->detached) return -3;  // Child has detached

  if (child->state == TASK_TERMINATED) {
    return child->exit_code;
  }

  return -1;  // Still running
}

// Readdir: copy filename at index into user buffer (via VFS)
static int sys_do_readdir(uint32_t index, char *buf, uint32_t size) {
  if (!buf || size == 0) return 0;
  return vfs_readdir("/", (int)index, buf, size);
}

// Getpid: return current task ID
static int sys_do_getpid(void) {
  task_t *current = task_current();
  return current ? (int)current->id : -1;
}

// Kill a task by task id.
static int sys_do_kill(uint32_t task_id) {
  return task_kill(task_id, -9);
}

// Main syscall dispatcher - called from assembly
// frame_ptr points to the iret frame on the kernel stack
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx,
                         uint32_t edx, void *frame) {
  switch (eax) {
    case SYS_WRITE:
      return (uint32_t)sys_do_write((int)ebx, (const char *)ecx, (size_t)edx);

    case SYS_EXIT:
      sys_do_exit((int)ebx);
      return 0;  // Never reached

    case SYS_YIELD:
      sys_do_yield();
      return 0;

    case SYS_EXEC:
      return (uint32_t)sys_do_exec((const char *)ebx, (iret_frame_t *)frame);

    case SYS_GFX_INIT:
      return sys_do_gfx_init();

    case SYS_GFX_EXIT:
      sys_do_gfx_exit();
      return 0;

    case SYS_GETKEY:
      return sys_do_getkey(ebx);

    case SYS_SPAWN:
      return (uint32_t)sys_do_spawn((const char *)ebx,
                                     (const char **)ecx, (int)edx);

    case SYS_WAIT:
      return (uint32_t)sys_do_wait(ebx);

    case SYS_READDIR:
      return (uint32_t)sys_do_readdir(ebx, (char *)ecx, edx);

    case SYS_GETPID:
      return (uint32_t)sys_do_getpid();

    case SYS_TASKINFO:
      task_list();
      return 0;

    case SYS_SHUTDOWN:
      printf("Shutting down...\n");
      cpu_shutdown();
      return 0;

    case SYS_WIN_CREATE: {
      int w = (int)(ebx >> 16);
      int h = (int)(ebx & 0xFFFF);
      task_t *cur = task_current();
      return cur ? (uint32_t)window_create(cur->id, w, h, (const char *)ecx)
                 : (uint32_t)-1;
    }

    case SYS_WIN_DESTROY: {
      task_t *cur = task_current();
      return cur ? (uint32_t)window_destroy((int)ebx, cur->id)
                 : (uint32_t)-1;
    }

    case SYS_WIN_WRITE: {
      task_t *cur = task_current();
      return cur ? (uint32_t)window_write((int)ebx, cur->id,
                                          (const uint8_t *)ecx, edx)
                 : (uint32_t)-1;
    }

    case SYS_WIN_READ:
      return (uint32_t)window_read((int)ebx, (uint8_t *)ecx, edx);

    case SYS_WIN_GETKEY: {
      task_t *cur = task_current();
      return cur ? (uint32_t)window_getkey((int)ebx, cur->id)
                 : (uint32_t)-1;
    }

    case SYS_WIN_SENDKEY:
      return (uint32_t)window_sendkey((int)ebx, (uint8_t)ecx);

    case SYS_WIN_LIST:
      return (uint32_t)window_list((win_info_t *)ebx, (int)ecx);

    case SYS_GFX_INFO:
      return sys_do_gfx_info();

    case SYS_TASKLIST:
      return (uint32_t)task_list_info((void *)ebx, (int)ecx);

    case SYS_WAIT_NB:
      return (uint32_t)sys_do_wait_nb(ebx);

    case SYS_PING:
      return (uint32_t)net_ping(ebx, ecx);

    case SYS_NETCFG:
      net_set_config(ebx, ecx, edx);
      return 0;

    case SYS_NETGET: {
      if (!ebx || !ecx || !edx) return (uint32_t)-1;
      uint32_t ip_be = 0, mask_be = 0, gw_be = 0;
      net_get_config(&ip_be, &mask_be, &gw_be);
      *(uint32_t *)ebx = ip_be;
      *(uint32_t *)ecx = mask_be;
      *(uint32_t *)edx = gw_be;
      return 0;
    }

    case SYS_SLEEPMS:
      return (uint32_t)sys_do_sleepms(ebx);

    case SYS_SOCK_LISTEN:
      return (uint32_t)net_sock_listen((uint16_t)ebx);

    case SYS_SOCK_ACCEPT:
      return (uint32_t)net_sock_accept((int)ebx);

    case SYS_SOCK_SEND:
      return (uint32_t)net_sock_send((int)ebx, (const void *)ecx, edx);

    case SYS_SOCK_RECV:
      return (uint32_t)net_sock_recv((int)ebx, (void *)ecx, edx);

    case SYS_SOCK_CLOSE:
      return (uint32_t)net_sock_close((int)ebx);

    case SYS_WIN_READ_TEXT: {
      task_t *cur = task_current();
      return cur ? (uint32_t)window_read_text((int)ebx, cur->id,
                                              (char *)ecx, (int)edx)
                 : (uint32_t)-1;
    }

    case SYS_WIN_SET_STDOUT: {
      task_t *cur = task_current();
      if (!cur) return (uint32_t)-1;
      cur->stdout_wid = (int)ebx;
      return 0;
    }

    case SYS_GETMOUSE: {
      mouse_state_t ms = mouse_get_state();
      if (ebx) *(int *)ebx = ms.x;
      if (ecx) *(int *)ecx = ms.y;
      if (edx) *(uint8_t *)edx = ms.buttons;
      return 0;
    }

    case SYS_OPEN: {
      task_t *cur = task_current();
      if (!cur || !cur->fd_table) return (uint32_t)-1;
      return (uint32_t)vfs_open(cur->fd_table, (const char *)ebx, (int)ecx);
    }

    case SYS_FREAD: {
      task_t *cur = task_current();
      if (!cur || !cur->fd_table) return (uint32_t)-1;
      return (uint32_t)vfs_read(cur->fd_table, (int)ebx, (void *)ecx, edx);
    }

    case SYS_FWRITE: {
      task_t *cur = task_current();
      if (!cur || !cur->fd_table) return (uint32_t)-1;
      return (uint32_t)vfs_write(cur->fd_table, (int)ebx, (const void *)ecx, edx);
    }

    case SYS_CLOSE: {
      task_t *cur = task_current();
      if (!cur || !cur->fd_table) return (uint32_t)-1;
      return (uint32_t)vfs_close(cur->fd_table, (int)ebx);
    }

    case SYS_SEEK: {
      task_t *cur = task_current();
      if (!cur || !cur->fd_table) return (uint32_t)-1;
      return (uint32_t)vfs_seek(cur->fd_table, (int)ebx, (int)ecx, (int)edx);
    }

    case SYS_STAT: {
      if (!ebx || !ecx) return (uint32_t)-1;
      return (uint32_t)vfs_stat((const char *)ebx, (vfs_stat_t *)ecx);
    }

    case SYS_DETACH:
      return (uint32_t)sys_do_detach();

    case SYS_UNLINK:
      if (!ebx) return (uint32_t)-1;
      return (uint32_t)vfs_unlink((const char *)ebx);

    case SYS_KILL:
      return (uint32_t)sys_do_kill(ebx);

    default:
      printf("[syscall] Unknown syscall %d\n", eax);
      return (uint32_t)-1;
  }
}

// Initialize syscall handler
void syscall_init(void) {
  printf("Syscall handler initializing...\n");
  // Note: The IDT entry for int 0x80 is set up in interrupts.c
  // with DPL=3 to allow user-mode access
  printf("Syscall handler ready (int 0x80)\n");
}
