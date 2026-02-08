#include "syscall.h"
#include "elf.h"
#include "lib.h"
#include "ramfs.h"
#include "task.h"
#include "arch/i686/paging.h"
#include "liballoc/liballoc_1_1.h"

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
  printf("[syscall] Task exiting with code %d\n", code);
  task_exit();
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

  printf("[syscall] exec('%s')\n", filename);

  // Look up file in ramfs
  ramfs_file_t *file = ramfs_lookup(filename);
  if (!file) {
    printf("[syscall] exec: file not found\n");
    return -1;
  }

  printf("[syscall] Loading ELF from 0x%x (%d bytes)\n", file->data, file->size);

  // Parse ELF header
  elf32_ehdr_t *elf = (elf32_ehdr_t *)file->data;
  if (!elf_validate(elf)) {
    printf("[syscall] exec: invalid ELF\n");
    return -1;
  }

  elf_print_info(elf);

  // Get current task
  task_t *current = task_current();
  if (!current || current->is_kernel) {
    printf("[syscall] exec: cannot exec from kernel task\n");
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

    printf("[syscall]   Loading segment: vaddr=0x%x memsz=%d filesz=%d\n",
           vaddr, memsz, filesz);

    // Allocate memory for segment
    // For simplicity, we'll just use the identity-mapped region
    // In a real OS, we'd allocate pages and map them

    // Mark pages as user-accessible
    for (uint32_t addr = vaddr; addr < vaddr + memsz; addr += 0x1000) {
      paging_set_user(addr & ~0xFFF);
    }

    // Copy segment data
    uint8_t *src = (uint8_t *)elf + offset;
    uint8_t *dst = (uint8_t *)vaddr;

    // Copy file data
    for (uint32_t j = 0; j < filesz; j++) {
      dst[j] = src[j];
    }

    // Zero remaining (BSS)
    for (uint32_t j = filesz; j < memsz; j++) {
      dst[j] = 0;
    }
  }

  printf("[syscall] Segments loaded, entry point: 0x%x\n", elf->e_entry);

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

  printf("[syscall] exec: returning to 0x%x\n", elf->e_entry);

  // Return 0 in eax - but the program won't see it since EIP changed
  return 0;
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
