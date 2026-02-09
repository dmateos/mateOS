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

// Track whether a user program is in graphics mode
static int user_gfx_active = 0;

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
  // Auto-cleanup graphics mode if this task had it active
  if (user_gfx_active) {
    keyboard_buffer_enable(0);
    vga_enter_text_mode();
    user_gfx_active = 0;
  }
  task_exit_with_code(code);
  // Should never return
}

// Yield to scheduler
static void sys_do_yield(void) {
  task_yield();
}

// Execute ELF binary from ramfs - replaces current process
// Uses per-process page tables: allocates physical frames from PMM,
// maps them at the ELF's virtual addresses in the task's page directory,
// and copies data via identity mapping.
static int sys_do_exec(const char *filename, iret_frame_t *frame) {
  if (!filename) {
    printf("[syscall] exec: NULL filename\n");
    return -1;
  }

  // Look up file in ramfs
  ramfs_file_t *file = ramfs_lookup(filename);
  if (!file) {
    printf("[exec] file not found: %s\n", filename);
    return -1;
  }

  // Parse ELF header
  elf32_ehdr_t *elf = (elf32_ehdr_t *)file->data;
  if (!elf_validate(elf)) {
    printf("[exec] invalid ELF: %s\n", filename);
    return -1;
  }

  task_t *current = task_current();
  if (!current || current->is_kernel || !current->page_dir) {
    return -1;
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

    // Process page by page
    uint32_t seg_start = vaddr & ~0xFFF;
    uint32_t seg_end = (vaddr + memsz + 0xFFF) & ~0xFFF;

    for (uint32_t page_vaddr = seg_start; page_vaddr < seg_end; page_vaddr += 0x1000) {
      // Allocate a physical frame
      uint32_t phys = pmm_alloc_frame();
      if (!phys) {
        printf("[exec] out of physical frames\n");
        return -1;
      }

      // Zero the frame (via identity mapping - phys < 32MB)
      memset((void *)phys, 0, 0x1000);

      // Copy ELF data that falls within this page
      // Calculate overlap between [vaddr, vaddr+filesz) and [page_vaddr, page_vaddr+0x1000)
      uint32_t copy_start = (page_vaddr > vaddr) ? page_vaddr : vaddr;
      uint32_t copy_end = (page_vaddr + 0x1000 < vaddr + filesz)
                            ? page_vaddr + 0x1000 : vaddr + filesz;

      if (copy_start < copy_end) {
        uint32_t dst_offset = copy_start - page_vaddr;
        uint32_t src_offset = copy_start - vaddr;
        memcpy((void *)(phys + dst_offset), src + src_offset, copy_end - copy_start);
      }

      // Map physical frame at virtual address in process page directory
      paging_map_page(current->page_dir, page_vaddr, phys,
                      PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
  }

  // Allocate a new user stack frame from PMM
  uint32_t stack_phys = pmm_alloc_frame();
  if (!stack_phys) {
    printf("[exec] failed to allocate stack frame\n");
    return -1;
  }
  memset((void *)stack_phys, 0, 0x1000);

  // Map user stack at virtual 0x7F0000
  uint32_t stack_virt = 0x7F0000;
  paging_map_page(current->page_dir, stack_virt, stack_phys,
                  PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

  // Flush TLB by reloading CR3 with our page directory
  paging_switch(current->page_dir);

  // Modify the iret frame to jump to ELF entry with new stack
  frame->eip    = elf->e_entry;
  frame->cs     = 0x1B;           // User code segment (RPL=3)
  frame->eflags = 0x202;          // IF=1, reserved bit 1
  frame->esp    = stack_virt + 0x1000;  // Top of stack page
  frame->ss     = 0x23;           // User data segment (RPL=3)

  return 0;
}

// Enter VGA Mode 13h and map framebuffer for user access
static uint32_t sys_do_gfx_init(void) {
  if (user_gfx_active) {
    return 0xA0000;  // Already active
  }

  vga_enter_mode13h();

  // Mark VGA framebuffer pages as user-accessible (0xA0000-0xAFFFF, 16 pages)
  for (uint32_t addr = 0xA0000; addr < 0xB0000; addr += 0x1000) {
    paging_set_user(addr);
  }

  keyboard_buffer_init();
  keyboard_buffer_enable(1);
  user_gfx_active = 1;

  return 0xA0000;
}

// Return to text mode
static void sys_do_gfx_exit(void) {
  if (!user_gfx_active) return;

  keyboard_buffer_enable(0);
  vga_enter_text_mode();
  user_gfx_active = 0;
}

// Read key from buffer (non-blocking)
static uint32_t sys_do_getkey(uint32_t flags __attribute__((unused))) {
  return (uint32_t)keyboard_buffer_pop();
}

// Spawn: create a child process from an ELF in ramfs
// Each child gets its own address space, so no code backup needed.
static void spawn_entry(void) {
  task_t *me = task_current();
  sys_exec(me->pending_exec);
  sys_exit(127);  // exec failed
}

static int sys_do_spawn(const char *filename) {
  if (!filename) return -1;

  // Check file exists first
  ramfs_file_t *file = ramfs_lookup(filename);
  if (!file) return -1;

  task_t *t = task_create_user("child", spawn_entry);
  if (!t) return -1;

  // Copy filename into child's pending_exec (race-safe per-task buffer)
  size_t i;
  for (i = 0; i < sizeof(t->pending_exec) - 1 && filename[i]; i++) {
    t->pending_exec[i] = filename[i];
  }
  t->pending_exec[i] = '\0';

  // Mark pending_exec buffer as user-accessible in child's address space
  // The buffer is in kernel BSS, which is in page table 0 (shared)
  uint32_t buf_addr = (uint32_t)t->pending_exec;
  paging_set_user(buf_addr & ~0xFFF);

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
