#include "task.h"
#include "lib.h"
#include "liballoc/liballoc_1_1.h"
#include "arch/i686/tss.h"
#include "arch/i686/paging.h"
#include "pmm.h"

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
    __asm__ volatile("hlt");
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
  idle->pending_exec[0] = '\0';

  current_task = idle;
  task_list_head = idle;

  printf("Task system initialized (kernel task id=0)\n");
}

task_t *task_create(const char *name, void (*entry)(void)) {
  // Find free task slot
  task_t *task = NULL;
  for (int i = 1; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_TERMINATED || tasks[i].id == 0) {
      task = &tasks[i];
      break;
    }
  }

  if (!task) {
    printf("Error: No free task slots\n");
    return NULL;
  }

  // Allocate stack
  uint32_t *stack = (uint32_t *)kmalloc(TASK_STACK_SIZE);
  if (!stack) {
    printf("Error: Failed to allocate task stack\n");
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
  task->pending_exec[0] = '\0';

  // Add to circular task list
  if (task_list_head == NULL) {
    task->next = task;
    task_list_head = task;
  } else {
    // Insert after current task
    task->next = current_task->next;
    current_task->next = task;
  }

  // task created silently

  return task;
}

// Create a user-mode task with its own address space
task_t *task_create_user(const char *name, void (*entry)(void)) {
  // Find free task slot
  task_t *task = NULL;
  for (int i = 1; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_TERMINATED || tasks[i].id == 0) {
      task = &tasks[i];
      break;
    }
  }

  if (!task) {
    printf("Error: No free task slots\n");
    return NULL;
  }

  // Create per-process address space
  page_directory_t *page_dir = paging_create_address_space();
  if (!page_dir) {
    printf("Error: Failed to create address space\n");
    return NULL;
  }

  // Allocate kernel stack (for interrupts/syscalls when in user mode)
  uint32_t *kernel_stack = (uint32_t *)kmalloc(TASK_STACK_SIZE);
  if (!kernel_stack) {
    printf("Error: Failed to allocate kernel stack\n");
    paging_destroy_address_space(page_dir);
    return NULL;
  }

  // Allocate user stack from PMM and map into process address space
  uint32_t user_stack_phys = pmm_alloc_frame();
  if (!user_stack_phys) {
    printf("Error: Failed to allocate user stack frame\n");
    kfree(kernel_stack);
    paging_destroy_address_space(page_dir);
    return NULL;
  }

  // Map user stack at virtual 0x7F0000 in the process's address space
  // Stack grows down, so ESP will start at 0x7F1000
  uint32_t user_stack_virt = 0x7F0000;
  paging_map_page(page_dir, user_stack_virt, user_stack_phys,
                  PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

  // Mark entry point pages as user-accessible in the process's page table
  // The entry is a kernel-space trampoline (e.g. spawn_entry) that will
  // call sys_exec to load the real ELF. Mark those pages in the per-process
  // page table 1 copy as user-accessible.
  uint32_t entry_addr = (uint32_t)entry;
  uint32_t entry_page = entry_addr & ~0xFFF;
  for (uint32_t i = 0; i < 4; i++) {
    // Set user bit on the per-process page table for these pages
    uint32_t addr = entry_page + i * 0x1000;
    uint32_t dir_idx = addr >> 22;
    uint32_t tbl_idx = (addr >> 12) & 0x3FF;
    if (page_dir->tables[dir_idx] & 0x1) {
      page_table_t *pt = (page_table_t *)(page_dir->tables[dir_idx] & ~0xFFF);
      pt->pages[tbl_idx] |= PAGE_USER;
      page_dir->tables[dir_idx] |= PAGE_USER;
    }
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
  task->stack = (uint32_t *)user_stack_phys;  // Track physical addr for cleanup
  task->entry = entry;
  task->page_dir = page_dir;
  task->pending_exec[0] = '\0';

  // User mode task
  task->is_kernel = 0;
  task->kernel_stack = kernel_stack;
  task->kernel_stack_top = (uint32_t)kernel_stack + TASK_STACK_SIZE;

  // Set up initial kernel stack for first context switch
  uint32_t *sp = (uint32_t *)task->kernel_stack_top;

  // User mode iret frame
  *(--sp) = USER_DATA_SEL;                             // SS
  *(--sp) = user_stack_virt + 0x1000;                  // ESP (top of user stack page)
  *(--sp) = 0x202;                                      // EFLAGS (IF=1)
  *(--sp) = USER_CODE_SEL;                             // CS
  *(--sp) = (uint32_t)entry;                           // EIP

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

  // Add to circular task list
  if (task_list_head == NULL) {
    task->next = task;
    task_list_head = task;
  } else {
    task->next = current_task->next;
    current_task->next = task;
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
  // Trigger a software interrupt or just call the scheduler
  // For now, we'll use a simple approach
  __asm__ volatile("int $0x20");  // Trigger timer interrupt
}

void task_exit_with_code(int code) {
  if (current_task && current_task->id != 0) {
    // Silently exit (no debug spam)
    current_task->state = TASK_TERMINATED;
    current_task->exit_code = code;

    // Wake up any task waiting for us
    for (int i = 0; i < MAX_TASKS; i++) {
      if (tasks[i].state == TASK_BLOCKED &&
          tasks[i].waiting_for == current_task->id) {
        tasks[i].state = TASK_READY;
        tasks[i].waiting_for = 0;
      }
    }

    // Switch to kernel page directory before freeing process pages
    paging_switch(paging_get_kernel_dir());

    // Destroy per-process address space (frees PMM frames + page tables)
    if (current_task->page_dir) {
      paging_destroy_address_space(current_task->page_dir);
      current_task->page_dir = NULL;
    }

    // Free the kernel stack (allocated from kernel heap)
    if (current_task->kernel_stack) {
      kfree(current_task->kernel_stack);
      current_task->kernel_stack = NULL;
    }
    // User stack was PMM-allocated and freed by paging_destroy_address_space
    current_task->stack = NULL;
  }

  // Yield to let scheduler pick next task
  while (1) {
    task_yield();
  }
}

void task_exit(void) {
  task_exit_with_code(0);
}

task_t *task_get_by_id(uint32_t id) {
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].id == id) {
      return &tasks[i];
    }
  }
  return NULL;
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
