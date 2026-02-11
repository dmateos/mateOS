#include "syscall.h"
#include "elf.h"
#include "lib.h"
#include "ramfs.h"
#include "task.h"
#include "keyboard.h"
#include "arch/i686/paging.h"
#include "arch/i686/io.h"
#include "arch/i686/vga.h"
#include "liballoc/liballoc_1_1.h"
#include "pmm.h"
#include "window.h"
#include "net.h"

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

// Write to console (fd is ignored for now - only stdout supported)
static int sys_do_write(int fd __attribute__((unused)),
                        const char *buf, size_t len) {
  // Basic validation - ensure buffer is in valid memory range
  // In a real OS, we'd check if the pointer is in user space
  if (buf == NULL || len == 0) {
    return -1;
  }

  // Write each character to console
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

// Load ELF segments into a page directory. Returns entry point, or 0 on error.
// Used by sys_do_exec, task_create_user_elf, and kernel shell launch.
uint32_t load_elf_into(struct page_directory *page_dir, const char *filename) {
  ramfs_file_t *file = ramfs_lookup(filename);
  if (!file) {
    printf("[exec] file not found: %s\n", filename);
    return 0;
  }

  elf32_ehdr_t *elf = (elf32_ehdr_t *)file->data;
  if (!elf_validate(elf)) {
    printf("[exec] invalid ELF: %s\n", filename);
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
    return 0;
  }
  memset((void *)stack_phys, 0, 0x1000);

  paging_map_page(page_dir, 0x7F0000, stack_phys,
                  PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

  return elf->e_entry;
}

// Execute ELF binary from ramfs - replaces current process
static int sys_do_exec(const char *filename, iret_frame_t *frame) {
  if (!filename) return -1;

  task_t *current = task_current();
  if (!current || current->is_kernel || !current->page_dir) {
    return -1;
  }

  uint32_t entry = load_elf_into(current->page_dir, filename);
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
// ELF is loaded entirely in kernel mode — no user-mode trampoline needed.
static int sys_do_spawn(const char *filename) {
  if (!filename) return -1;

  task_t *t = task_create_user_elf(filename);
  if (!t) return -1;

  if (!task_is_enabled()) {
    task_enable();
  }

  return (int)t->id;
}

// Wait: block until a child task exits, return its exit code
static int sys_do_wait(uint32_t task_id) {
  task_t *child = task_get_by_id(task_id);
  if (!child) return -1;

  if (child->state == TASK_TERMINATED) {
    return child->exit_code;
  }

  // Block current task until child exits
  task_t *current = task_current();
  current->waiting_for = task_id;
  current->state = TASK_BLOCKED;
  task_yield();

  current->waiting_for = 0;

  return child->exit_code;
}

// Non-blocking wait: returns -1 if child still running, else exit code
static int sys_do_wait_nb(uint32_t task_id) {
  task_t *child = task_get_by_id(task_id);
  if (!child) return -2;  // No such task

  if (child->state == TASK_TERMINATED) {
    return child->exit_code;
  }

  return -1;  // Still running
}

// Readdir: copy filename at index from ramfs into user buffer
static int sys_do_readdir(uint32_t index, char *buf, uint32_t size) {
  if (!buf || size == 0) return 0;

  ramfs_file_t *file = ramfs_get_file_by_index((int)index);
  if (!file) return 0;

  // Copy filename to user buffer
  size_t name_len = strlen(file->name);
  if (name_len >= size) name_len = size - 1;
  memcpy(buf, file->name, name_len);
  buf[name_len] = '\0';

  return (int)(name_len + 1);
}

// Getpid: return current task ID
static int sys_do_getpid(void) {
  task_t *current = task_current();
  return current ? (int)current->id : -1;
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
      return (uint32_t)sys_do_spawn((const char *)ebx);

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
      outw(0x604, 0x2000);  // QEMU ACPI shutdown
      // If that didn't work, halt
      while (1) { __asm__ volatile("cli; hlt"); }
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
