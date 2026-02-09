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
static int sys_do_exec(const char *filename, iret_frame_t *frame) {
  // Validate filename pointer (in user space)
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

  // Get current task
  task_t *current = task_current();
  if (!current || current->is_kernel) {
    return -1;
  }

  // Load program segments
  elf32_phdr_t *phdr = (elf32_phdr_t *)((uint8_t *)elf + elf->e_phoff);

  for (int i = 0; i < elf->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) {
      continue;
    }

    uint32_t vaddr = phdr[i].p_vaddr;
    uint32_t memsz = phdr[i].p_memsz;
    uint32_t filesz = phdr[i].p_filesz;
    uint32_t offset = phdr[i].p_offset;

    // Mark pages as user-accessible
    for (uint32_t addr = vaddr; addr < vaddr + memsz; addr += 0x1000) {
      paging_set_user(addr & ~0xFFF);
    }

    // Copy segment data
    uint8_t *src = (uint8_t *)elf + offset;
    uint8_t *dst = (uint8_t *)vaddr;

    for (uint32_t j = 0; j < filesz; j++) {
      dst[j] = src[j];
    }

    // Zero BSS
    for (uint32_t j = filesz; j < memsz; j++) {
      dst[j] = 0;
    }
  }

  // Allocate new user stack for the loaded program
  uint32_t *new_user_stack = (uint32_t *)kmalloc(TASK_STACK_SIZE);
  if (!new_user_stack) {
    printf("[syscall] exec: failed to allocate user stack\n");
    return -1;
  }

  // Mark stack pages as user-accessible
  // Must mark all pages from base through top (inclusive of page containing top)
  uint32_t stack_addr = (uint32_t)new_user_stack;
  uint32_t stack_top = stack_addr + TASK_STACK_SIZE;
  for (uint32_t addr = stack_addr & ~0xFFF; addr <= (stack_top & ~0xFFF); addr += 0x1000) {
    paging_set_user(addr);
  }

  uint32_t new_user_esp = stack_top;

  // Free old user stack
  if (current->stack) {
    kfree(current->stack);
  }
  current->stack = new_user_stack;

  // Modify the iret frame directly on the kernel stack.
  // When isr128 does popa + iret, it will return to the ELF entry point
  // in user mode with the new stack.
  frame->eip    = elf->e_entry;  // Jump to ELF entry
  frame->cs     = 0x1B;          // User code segment (RPL=3)
  frame->eflags = 0x202;         // IF=1, reserved bit 1
  frame->esp    = new_user_esp;  // New user stack
  frame->ss     = 0x23;          // User data segment (RPL=3)

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
// Since all user ELFs are linked to 0x700000, we save the parent's code
// region before loading the child and restore it when the child exits.
#define USER_CODE_BASE  0x700000
#define USER_CODE_SIZE  0x10000   // 64KB should cover any user program

static char spawn_filename[64];
static uint8_t *code_backup = NULL;  // Backup of parent's code region

static void spawn_entry(void) {
  sys_exec(spawn_filename);
  sys_exit(127);  // exec failed
}

static int sys_do_spawn(const char *filename) {
  if (!filename) return -1;

  // Copy filename to static buffer
  size_t i;
  for (i = 0; i < sizeof(spawn_filename) - 1 && filename[i]; i++) {
    spawn_filename[i] = filename[i];
  }
  spawn_filename[i] = '\0';

  // Check file exists
  ramfs_file_t *file = ramfs_lookup(spawn_filename);
  if (!file) {
    return -1;
  }

  // Save parent's code region before child overwrites it
  if (code_backup) {
    kfree(code_backup);
  }
  code_backup = (uint8_t *)kmalloc(USER_CODE_SIZE);
  if (!code_backup) {
    printf("[spawn] failed to allocate backup\n");
    return -1;
  }
  memcpy(code_backup, (void *)USER_CODE_BASE, USER_CODE_SIZE);

  // Mark spawn_filename page as user-accessible
  paging_set_user((uint32_t)spawn_filename & ~0xFFF);

  task_t *t = task_create_user("child", spawn_entry);
  if (!t) {
    kfree(code_backup);
    code_backup = NULL;
    return -1;
  }

  if (!task_is_enabled()) {
    task_enable();
  }

  return (int)t->id;
}

// Wait: block until a child task exits, return its exit code
static int sys_do_wait(uint32_t task_id) {
  task_t *child = task_get_by_id(task_id);
  if (!child) return -1;

  // If child already exited, return immediately
  if (child->state == TASK_TERMINATED) {
    // Restore parent's code region
    if (code_backup) {
      memcpy((void *)USER_CODE_BASE, code_backup, USER_CODE_SIZE);
      kfree(code_backup);
      code_backup = NULL;
    }
    return child->exit_code;
  }

  // Block current task until child exits
  task_t *current = task_current();
  current->waiting_for = task_id;
  current->state = TASK_BLOCKED;
  task_yield();  // trigger reschedule

  // When we resume, child has exited - restore parent's code
  current->waiting_for = 0;
  if (code_backup) {
    memcpy((void *)USER_CODE_BASE, code_backup, USER_CODE_SIZE);
    kfree(code_backup);
    code_backup = NULL;
  }
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
