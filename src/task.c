#include "task.h"
#include "syscall.h"  // for load_elf_into
#include "lib.h"
#include "vfs.h"
#include "liballoc/liballoc_1_1.h"
#include "arch/i686/tss.h"
#include "arch/i686/paging.h"
#include "arch/i686/cpu.h"
#include "pmm.h"
#include "window.h"

// Task array and management
static task_t tasks[MAX_TASKS];
static task_t *current_task = NULL;
static task_t *task_list_head = NULL;
static uint32_t next_task_id = 1;
static int multitasking_enabled = 0;

// Idle task - runs when no other task is ready
static void idle_task_entry(void) {
  while (1) {
    // Just halt and wait for interrupts
    cpu_halt();
  }
}

// Wrapper to handle task exit
static void task_entry_wrapper(void) {
  // Get the current task's entry point and call it
  if (current_task && current_task->entry) {
    current_task->entry();
  }
  // If task returns, terminate it
  task_exit();
}

void task_init(void) {
  printf("Task system initializing...\n");

  // Clear task array
  memset(tasks, 0, sizeof(tasks));

  // Create the idle/kernel task (task 0) - represents the current execution context
  task_t *idle = &tasks[0];
  idle->id = 0;
  memcpy(idle->name, "kernel", 7);
  idle->state = TASK_RUNNING;
  idle->stack = NULL;  // Uses existing kernel stack
  idle->stack_top = NULL;
  idle->entry = NULL;
  idle->next = idle;  // Point to self initially
  idle->is_kernel = 1;
  idle->kernel_stack = NULL;
  idle->kernel_stack_top = 0;
  idle->page_dir = NULL;

  current_task = idle;
  task_list_head = idle;

  printf("Task system initialized (kernel task id=0)\n");
}

task_t *task_create(const char *name, void (*entry)(void)) {
  // Find free task slot
  task_t *task = NULL;
  int reusing = 0;
  for (int i = 1; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_TERMINATED) {
      task = &tasks[i];
      reusing = 1;
      // Free leftover kernel stack from terminated task
      if (task->kernel_stack) {
        kfree(task->kernel_stack);
        task->kernel_stack = NULL;
      }
      break;
    }
    if (tasks[i].id == 0) {
      task = &tasks[i];
      break;
    }
  }

  if (!task) {
    kprintf("Error: No free task slots\n");
    return NULL;
  }

  // Allocate stack
  uint32_t *stack = (uint32_t *)kmalloc(TASK_STACK_SIZE);
  if (!stack) {
    kprintf("Error: Failed to allocate task stack\n");
    return NULL;
  }

  // Initialize task
  task->id = next_task_id++;
  size_t name_len = strlen(name);
  if (name_len >= TASK_NAME_MAX) {
    name_len = TASK_NAME_MAX - 1;
  }
  memcpy(task->name, name, name_len);
  task->name[name_len] = '\0';
  task->state = TASK_READY;
  task->stack = stack;
  task->entry = entry;

  // Set up initial stack for context switch
  // Stack grows downward, so start at top
  uint32_t *sp = (uint32_t *)((uint8_t *)stack + TASK_STACK_SIZE);

  // Push initial CPU state (what the interrupt handler expects)
  // These will be popped by iret
  *(--sp) = 0x202;                       // EFLAGS (IF=1, reserved bit 1 = 1)
  *(--sp) = 0x08;                        // CS (kernel code segment)
  *(--sp) = (uint32_t)task_entry_wrapper; // EIP - start at wrapper

  // Pushed by pusha (in reverse order since stack grows down)
  *(--sp) = 0;  // EAX
  *(--sp) = 0;  // ECX
  *(--sp) = 0;  // EDX
  *(--sp) = 0;  // EBX
  *(--sp) = 0;  // ESP (ignored by popa)
  *(--sp) = 0;  // EBP
  *(--sp) = 0;  // ESI
  *(--sp) = 0;  // EDI

  // Segment registers (for kernel task, use kernel data segment)
  *(--sp) = KERNEL_DATA_SEG;  // GS
  *(--sp) = KERNEL_DATA_SEG;  // FS
  *(--sp) = KERNEL_DATA_SEG;  // ES
  *(--sp) = KERNEL_DATA_SEG;  // DS

  task->stack_top = sp;

  // Kernel mode task
  task->is_kernel = 1;
  task->kernel_stack = NULL;
  task->kernel_stack_top = 0;
  task->page_dir = NULL;
  task->stdout_wid = -1;
  task->detached = 0;

  // Add to circular task list (skip if reusing — already linked)
  if (!reusing) {
    if (task_list_head == NULL) {
      task->next = task;
      task_list_head = task;
    } else {
      task->next = current_task->next;
      current_task->next = task;
    }
  }

  return task;
}

// Create a user-mode task by loading an ELF from ramfs.
// The ELF is loaded entirely in kernel mode — the task starts directly
// at the ELF entry point with no kernel trampoline. No kernel pages are
// marked user-accessible.
// If argv==NULL or argc<=0, defaults to argc=1 with argv={filename}.
task_t *task_create_user_elf(const char *filename, const char **argv, int argc) {
  // Default argv if not provided
  const char *default_argv[1];
  if (!argv || argc <= 0) {
    default_argv[0] = filename;
    argv = default_argv;
    argc = 1;
  }

  // Find free task slot
  task_t *task = NULL;
  int reusing = 0;
  for (int i = 1; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_TERMINATED) {
      task = &tasks[i];
      reusing = 1;
      // Free leftover kernel stack from terminated task
      if (task->kernel_stack) {
        kfree(task->kernel_stack);
        task->kernel_stack = NULL;
      }
      break;
    }
    if (tasks[i].id == 0) {
      task = &tasks[i];
      break;
    }
  }

  if (!task) {
    kprintf("Error: No free task slots\n");
    return NULL;
  }

  // Create per-process address space
  page_directory_t *page_dir = paging_create_address_space();
  if (!page_dir) {
    kprintf("Error: Failed to create address space\n");
    return NULL;
  }

  // Load ELF into the new address space (allocates code + stack pages)
  uint32_t stack_phys = 0;
  uint32_t elf_entry = load_elf_into(page_dir, filename, &stack_phys);
  if (!elf_entry) {
    paging_destroy_address_space(page_dir);
    return NULL;
  }

  // --- Place argc/argv on user stack ---
  // The stack page physical address is identity-mapped in kernel space,
  // so we can write directly to stack_phys. Virtual address = 0x7F0000.
  // Layout (top-down):
  //   strings packed at top of page
  //   argv[argc] = NULL
  //   argv[0..argc-1] = pointers to strings (virtual addrs)
  //   argv pointer (char **)
  //   argc
  //   ESP points here on entry

  uint8_t *page = (uint8_t *)stack_phys;
  uint32_t str_off = 0x1000;  // start from top, grow down

  // Step 1: Copy arg strings to top of stack page (top-down)
  uint32_t str_vaddrs[16];  // max 16 args
  if (argc > 16) argc = 16;

  for (int i = argc - 1; i >= 0; i--) {
    size_t slen = strlen(argv[i]) + 1;  // include null terminator
    if (str_off < slen + 64) break;     // safety: keep room for pointers
    str_off -= slen;
    memcpy(page + str_off, argv[i], slen);
    str_vaddrs[i] = 0x7F0000 + str_off;
  }

  // Step 2: Align down to 4-byte boundary
  str_off &= ~3;

  // Step 3: Write argv[] array + NULL terminator (below strings)
  // argv[argc] = NULL, then argv[argc-1] .. argv[0]
  str_off -= 4;
  *(uint32_t *)(page + str_off) = 0;  // argv[argc] = NULL
  uint32_t argv_end = str_off;  // remember where NULL is

  for (int i = argc - 1; i >= 0; i--) {
    str_off -= 4;
    *(uint32_t *)(page + str_off) = str_vaddrs[i];
  }
  uint32_t argv_vaddr = 0x7F0000 + str_off;  // virtual addr of argv[0]
  (void)argv_end;

  // Step 4: Write argc, argv pointer, and fake return address below the array.
  // gcc compiles _start(int argc, char **argv) with cdecl convention,
  // expecting [ret_addr][argc][argv] on the stack. Since iret jumps
  // directly (no 'call'), we must place a dummy return address.
  str_off -= 4;
  *(uint32_t *)(page + str_off) = argv_vaddr;  // char **argv  (esp+8)
  str_off -= 4;
  *(uint32_t *)(page + str_off) = (uint32_t)argc;  // int argc  (esp+4)
  str_off -= 4;
  *(uint32_t *)(page + str_off) = 0;  // fake return address  (esp)

  uint32_t user_esp = 0x7F0000 + str_off;

  // Allocate kernel stack (for interrupts/syscalls when in user mode)
  uint32_t *kernel_stack = (uint32_t *)kmalloc(TASK_STACK_SIZE);
  if (!kernel_stack) {
    kprintf("Error: Failed to allocate kernel stack\n");
    paging_destroy_address_space(page_dir);
    return NULL;
  }

  // Initialize task
  task->id = next_task_id++;
  size_t name_len = strlen(filename);
  if (name_len >= TASK_NAME_MAX) {
    name_len = TASK_NAME_MAX - 1;
  }
  memcpy(task->name, filename, name_len);
  task->name[name_len] = '\0';
  task->state = TASK_READY;
  task->stack = NULL;
  task->entry = NULL;
  task->page_dir = page_dir;

  // User mode task
  task->is_kernel = 0;
  task->kernel_stack = kernel_stack;
  task->kernel_stack_top = (uint32_t)kernel_stack + TASK_STACK_SIZE;

  // Set up initial kernel stack for first context switch.
  // The iret frame points directly at the ELF entry point in user mode.
  uint32_t *sp = (uint32_t *)task->kernel_stack_top;

  // User mode iret frame
  *(--sp) = USER_DATA_SEL;          // SS
  *(--sp) = user_esp;               // ESP (points at argc on user stack)
  *(--sp) = 0x202;                  // EFLAGS (IF=1)
  *(--sp) = USER_CODE_SEL;          // CS
  *(--sp) = elf_entry;              // EIP - directly at ELF entry point

  // Pushed by pusha
  *(--sp) = 0;  // EAX
  *(--sp) = 0;  // ECX
  *(--sp) = 0;  // EDX
  *(--sp) = 0;  // EBX
  *(--sp) = 0;  // ESP (ignored)
  *(--sp) = 0;  // EBP
  *(--sp) = 0;  // ESI
  *(--sp) = 0;  // EDI

  // Segment registers
  *(--sp) = USER_DATA_SEL;  // GS
  *(--sp) = USER_DATA_SEL;  // FS
  *(--sp) = USER_DATA_SEL;  // ES
  *(--sp) = USER_DATA_SEL;  // DS

  task->stack_top = sp;
  task->stdout_wid = -1;
  task->detached = 0;

  // Allocate per-task file descriptor table
  task->fd_table = (vfs_fd_table_t *)kmalloc(sizeof(vfs_fd_table_t));
  if (task->fd_table) {
    memset(task->fd_table, 0, sizeof(vfs_fd_table_t));
  }

  // Add to circular task list (skip if reusing — already linked)
  if (!reusing) {
    if (task_list_head == NULL) {
      task->next = task;
      task_list_head = task;
    } else {
      task->next = current_task->next;
      current_task->next = task;
    }
  }

  return task;
}

task_t *task_current(void) {
  return current_task;
}

int task_is_enabled(void) {
  return multitasking_enabled;
}

// Round-robin scheduler - called from timer interrupt
// current_esp is the stack pointer of the interrupted task
// Returns the stack pointer to switch to
uint32_t *schedule(uint32_t *current_esp) {
  if (!multitasking_enabled || !current_task) {
    return current_esp;
  }

  // Save current task's stack pointer
  current_task->stack_top = current_esp;

  // Mark current task as ready (unless it's terminated or blocked)
  if (current_task->state == TASK_RUNNING) {
    current_task->state = TASK_READY;
  }

  // Find next ready task (round-robin)
  task_t *next = current_task->next;
  task_t *start = next;

  do {
    if (next->state == TASK_READY) {
      break;
    }
    next = next->next;
  } while (next != start);

  // If no ready task found, stay on current (or idle)
  if (next->state != TASK_READY) {
    // Fall back to kernel/idle task
    next = &tasks[0];
  }

  // Switch to next task
  current_task = next;
  current_task->state = TASK_RUNNING;

  // Update TSS with new task's kernel stack for user mode tasks
  if (!current_task->is_kernel && current_task->kernel_stack_top) {
    tss_set_kernel_stack(current_task->kernel_stack_top);
  }

  // Switch address space (CR3)
  if (current_task->page_dir) {
    paging_switch(current_task->page_dir);
  } else {
    paging_switch(paging_get_kernel_dir());
  }

  return current_task->stack_top;
}

void task_yield(void) {
  // Use dedicated yield interrupt (0x81) instead of timer interrupt (0x20)
  // to avoid sending spurious EOI to the PIC, which corrupts its state
  cpu_yield_interrupt();
}

static void task_terminate(task_t *task, int code) {
  if (!task || task->id == 0 || task->state == TASK_TERMINATED) return;

  task->state = TASK_TERMINATED;
  task->exit_code = code;

  // Wake up any task waiting for this task.
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_BLOCKED && tasks[i].waiting_for == task->id) {
      tasks[i].state = TASK_READY;
      tasks[i].waiting_for = 0;
    }
  }

  // Clean up any windows owned by this process.
  window_cleanup_pid(task->id);

  // Close all open file descriptors.
  if (task->fd_table) {
    vfs_close_all(task->fd_table);
    kfree(task->fd_table);
    task->fd_table = NULL;
  }

  // Free user address-space resources from kernel address space.
  paging_switch(paging_get_kernel_dir());
  if (task->page_dir) {
    paging_destroy_address_space(task->page_dir);
    task->page_dir = NULL;
  }

  task->stack = NULL;
}

void task_exit_with_code(int code) {
  if (current_task && current_task->id != 0) {
    // NOTE: Do NOT free kernel_stack here — we are currently executing on it.
    // The kernel stack will be freed when the task slot is reused.
    task_terminate(current_task, code);
  }

  // Yield to let scheduler pick next task
  while (1) {
    task_yield();
  }
}

void task_exit(void) {
  task_exit_with_code(0);
}

int task_kill(uint32_t task_id, int code) {
  task_t *task = task_get_by_id(task_id);
  if (!task || task->id == 0 || task->is_kernel) return -1;
  if (task->state == TASK_TERMINATED) return -2;

  if (task == current_task) {
    task_exit_with_code(code);
    return 0;
  }

  task_terminate(task, code);
  return 0;
}

task_t *task_get_by_id(uint32_t id) {
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].id == id) {
      return &tasks[i];
    }
  }
  return NULL;
}

task_t *task_get_by_index(int idx) {
  if (idx < 0 || idx >= MAX_TASKS) return NULL;
  return &tasks[idx];
}

// Fill user buffer with task info, return count
int task_list_info(taskinfo_entry_t *buf, int max) {
  int count = 0;
  for (int i = 0; i < MAX_TASKS && count < max; i++) {
    if (tasks[i].id == 0 && i != 0) continue;
    if (tasks[i].state == TASK_TERMINATED) continue;

    buf[count].id = tasks[i].id;
    buf[count].state = (uint32_t)tasks[i].state;

    // Copy name
    int j;
    for (j = 0; j < TASK_NAME_MAX - 1 && tasks[i].name[j]; j++) {
      buf[count].name[j] = tasks[i].name[j];
    }
    buf[count].name[j] = '\0';

    count++;
  }
  return count;
}

void task_list(void) {
  printf("Task List:\n");
  printf("  ID  State      Ring  Name\n");
  printf("  --  ---------  ----  ----\n");

  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].id != 0 || i == 0) {
      if (tasks[i].state == TASK_TERMINATED && i != 0) {
        continue;  // Skip terminated tasks (except kernel)
      }

      const char *state_str;
      switch (tasks[i].state) {
        case TASK_READY:      state_str = "ready    "; break;
        case TASK_RUNNING:    state_str = "running  "; break;
        case TASK_BLOCKED:    state_str = "blocked  "; break;
        case TASK_TERMINATED: state_str = "terminated"; break;
        default:              state_str = "unknown  "; break;
      }

      printf("  %d   %s  %d     %s%s\n",
             tasks[i].id,
             state_str,
             tasks[i].is_kernel ? 0 : 3,
             tasks[i].name,
             (&tasks[i] == current_task) ? " *" : "");
    }
  }
}

// Enable multitasking (called after creating initial tasks)
void task_enable(void) {
  multitasking_enabled = 1;
  printf("Multitasking enabled\n");
}
