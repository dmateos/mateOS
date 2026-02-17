#ifndef _TASK_H
#define _TASK_H

#include "lib.h"
#include "arch/i686/paging.h"
#include "vfs.h"

// Task states
typedef enum {
  TASK_READY = 0,
  TASK_RUNNING = 1,
  TASK_BLOCKED = 2,
  TASK_TERMINATED = 3
} task_state_t;

// CPU registers saved during context switch (kernel mode)
typedef struct {
  // Pushed by pusha
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp_dummy;  // Ignored by popa
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;

  // Pushed by interrupt handler
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
} __attribute__((packed)) cpu_state_t;

// Extended CPU state for user mode (includes user SS and ESP for ring transition)
typedef struct {
  // Pushed by pusha
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp_dummy;  // Ignored by popa
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;

  // Pushed by CPU on interrupt from user mode
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t user_esp;   // Only present on ring transition (user->kernel)
  uint32_t user_ss;    // Only present on ring transition (user->kernel)
} __attribute__((packed)) cpu_state_user_t;

// Task Control Block
#define TASK_NAME_MAX 32
#define TASK_STACK_SIZE 4096

typedef struct task {
  uint32_t id;                    // Task ID
  uint32_t parent_id;             // Parent task ID (0 for kernel/root)
  char name[TASK_NAME_MAX];       // Task name
  task_state_t state;             // Current state

  uint32_t *stack;                // Stack base (allocated memory)
  uint32_t *stack_top;            // Current stack pointer (ESP)

  void (*entry)(void);            // Entry point function

  struct task *next;              // Next task in list (circular)

  // User mode support
  int is_kernel;                  // 1 = kernel mode task, 0 = user mode task
  uint32_t *kernel_stack;         // Kernel stack for user mode tasks (for TSS)
  uint32_t kernel_stack_top;      // Top of kernel stack (for TSS ESP0)

  // Process management
  int exit_code;                  // Exit code set by sys_exit
  uint32_t waiting_for;           // Task ID this task is blocked waiting for (0 = not waiting)
  uint32_t runtime_ticks;         // CPU runtime accumulated on timer IRQ ticks

  // Detach flag: process has detached from parent's wait
  int detached;

  // Per-process address space
  page_directory_t *page_dir;     // Per-process page directory (NULL for kernel tasks)
  uint32_t user_brk_min;          // Lowest allowed user brk (typically end of loaded image)
  uint32_t user_brk;              // Current user brk (program break)

  // stdout redirection: window ID for write(1,...) output (-1 = kernel console)
  int stdout_wid;

  // Per-task file descriptors
  vfs_fd_table_t *fd_table;
} task_t;

// Maximum number of tasks
#define MAX_TASKS 16

// Initialize the task system
void task_init(void);

// Create a new kernel-mode task
task_t *task_create(const char *name, void (*entry)(void));

// Create a new user-mode task by loading an ELF from ramfs
// If argv/argc are provided, places them on the user stack for _start(argc, argv).
// If argv==NULL or argc==0, defaults to argc=1 with argv={filename}.
task_t *task_create_user_elf(const char *filename, const char **argv, int argc);

// Get current running task
task_t *task_current(void);

// Yield CPU to next task (cooperative)
void task_yield(void);

// Called from timer interrupt for preemptive scheduling
// Returns new stack pointer to switch to
uint32_t *schedule(uint32_t *current_esp, uint32_t is_hw_tick);

// Terminate current task
void task_exit(void);
void task_exit_with_code(int code);
int task_kill(uint32_t task_id, int code);

// Print task list
void task_list(void);

// Fill user buffer with task info
// Uses taskinfo_entry_t from syscall.h
#include "syscall.h"
int task_list_info(taskinfo_entry_t *buf, int max);

// Look up a task by its ID
task_t *task_get_by_id(uint32_t id);

// Look up a task by array index (0..MAX_TASKS-1)
task_t *task_get_by_index(int idx);

// Check if multitasking is enabled
int task_is_enabled(void);

// Enable preemptive multitasking
void task_enable(void);

#endif
