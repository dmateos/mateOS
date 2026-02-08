#include "console.h"
#include "lib.h"
#include "arch/i686/timer.h"
#include "liballoc/liballoc_1_1.h"
#include "ramfs.h"
#include "task.h"
#include "syscall.h"

// Console state
static char line_buffer[CONSOLE_LINE_MAX];
static size_t line_position = 0;

extern void kb_reboot(void);

// Command handler function type
typedef void (*command_handler_t)(int argc, char **argv);

// Command structure
typedef struct {
  const char *name;
  const char *description;
  command_handler_t handler;
} command_t;

// Forward declarations of command handlers
static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_uptime(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_rust(int argc, char **argv);
static void cmd_memtest(int argc, char **argv);
static void cmd_tasks(int argc, char **argv);
static void cmd_spawn(int argc, char **argv);
static void cmd_usertest(int argc, char **argv);
static void cmd_demo(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_exec(int argc, char **argv);
static void cmd_test(int argc, char **argv);

// Command table
static const command_t commands[] = {
    {"help", "Show this help message", cmd_help},
    {"clear", "Clear the screen", cmd_clear},
    {"echo", "Print arguments", cmd_echo},
    {"uptime", "Show system uptime", cmd_uptime},
    {"reboot", "Reboot the system", cmd_reboot},
    {"rust", "Test Rust integration", cmd_rust},
    {"memtest", "Test memory allocator", cmd_memtest},
    {"tasks", "List running tasks", cmd_tasks},
    {"spawn", "Spawn kernel-mode test tasks", cmd_spawn},
    {"usertest", "Spawn a user-mode test task", cmd_usertest},
    {"demo", "Run mixed Ring 0 + Ring 3 multitasking demo", cmd_demo},
    {"ls", "List files in ramfs", cmd_ls},
    {"exec", "Execute ELF binary from ramfs", cmd_exec},
    {"test", "Run comprehensive userland test suite", cmd_test},
};

static const size_t command_count = sizeof(commands) / sizeof(commands[0]);

// Print the prompt
static void print_prompt(void) { printf("mateOS> "); }

// Command implementations
static void cmd_help(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("Available commands:\n");
  for (size_t i = 0; i < command_count; i++) {
    printf("  %s - %s\n", commands[i].name, commands[i].description);
  }
}

static void cmd_clear(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
  // Clear screen by printing newlines (simple approach)
  for (int i = 0; i < 25; i++) {
    printf("\n");
  }
}

static void cmd_echo(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    printf("%s", argv[i]);
    if (i < argc - 1) {
      printf(" ");
    }
  }
  printf("\n");
}

static void cmd_uptime(int argc __attribute__((unused)),
                       char **argv __attribute__((unused))) {
  uint32_t seconds = get_uptime_seconds();
  uint32_t ticks = get_tick_count();

  uint32_t hours = seconds / 3600;
  uint32_t minutes = (seconds % 3600) / 60;
  uint32_t secs = seconds % 60;

  printf("Uptime: %d:%d:%d (%d seconds, %d ticks)\n", hours, minutes, secs,
         seconds, ticks);
}

static void cmd_reboot(int argc __attribute__((unused)),
                       char **argv __attribute__((unused))) {
  printf("Rebooting...\n");
  // Wait a moment for the message to display
  for (volatile int i = 0; i < 10000000; i++)
    ;

  kb_reboot();

  // If that didn't work, triple fault
//  __asm__ volatile("lidt 0");
}

// External Rust functions
extern void rust_hello(void);
extern int rust_add(int a, int b);
extern unsigned int rust_factorial(unsigned int n);
extern void rust_fizzbuzz(unsigned int n);
extern unsigned int rust_sum_of_squares(unsigned int n);

static void cmd_rust(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("Testing Rust integration:\n\n");

  // Test 1: Hello
  rust_hello();

  // Test 2: Math operations
  printf("Rust add(2, 3) = %d\n", rust_add(2, 3));
  printf("Rust factorial(5) = %d\n", rust_factorial(5));
  printf("Rust sum_of_squares(5) = %d\n", rust_sum_of_squares(5));

  printf("\nFizzBuzz (1-15):\n");
  rust_fizzbuzz(15);

  printf("\nRust integration working!\n");
}

static void cmd_memtest(int argc __attribute__((unused)),
                        char **argv __attribute__((unused))) {
  printf("Testing memory allocator (liballoc):\n\n");

  // Test 1: Simple allocation
  printf("Test 1: Allocating 64 bytes... ");
  void *ptr1 = kmalloc(64);
  if (ptr1) {
    printf("OK at 0x%x\n", (uint32_t)ptr1);
  } else {
    printf("FAILED\n");
    return;
  }

  // Test 2: Write to allocated memory
  printf("Test 2: Writing to memory... ");
  char *str = (char *)ptr1;
  str[0] = 'H';
  str[1] = 'i';
  str[2] = '!';
  str[3] = '\0';
  printf("wrote '%s', ", str);
  if (str[0] == 'H' && str[1] == 'i' && str[2] == '!') {
    printf("read back OK\n");
  } else {
    printf("FAILED\n");
  }

  // Test 3: Multiple allocations
  printf("Test 3: Multiple allocations...\n");
  void *ptr2 = kmalloc(128);
  void *ptr3 = kmalloc(256);
  void *ptr4 = kmalloc(512);
  printf("  128 bytes at 0x%x\n", (uint32_t)ptr2);
  printf("  256 bytes at 0x%x\n", (uint32_t)ptr3);
  printf("  512 bytes at 0x%x\n", (uint32_t)ptr4);

  if (ptr2 && ptr3 && ptr4) {
    printf("  All allocations succeeded\n");
  } else {
    printf("  Some allocations FAILED\n");
    return;
  }

  // Test 4: Free and reallocate
  printf("Test 4: Free and reallocate... ");
  kfree(ptr2);
  kfree(ptr3);
  void *ptr5 = kmalloc(100);
  if (ptr5) {
    printf("OK at 0x%x\n", (uint32_t)ptr5);
  } else {
    printf("FAILED\n");
  }

  // Test 5: Large allocation
  printf("Test 5: Large allocation (4KB)... ");
  void *ptr6 = kmalloc(4096);
  if (ptr6) {
    printf("OK at 0x%x\n", (uint32_t)ptr6);
  } else {
    printf("FAILED\n");
  }

  // Clean up
  kfree(ptr1);
  kfree(ptr4);
  kfree(ptr5);
  kfree(ptr6);

  printf("\nMemory allocator tests complete!\n");
}

static void cmd_tasks(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
  task_list();
}

// Test task functions
static volatile int task_a_counter = 0;
static volatile int task_b_counter = 0;
static volatile int task_c_counter = 0;

static void test_task_a(void) {
  while (1) {
    task_a_counter++;
    // Busy wait a bit
    for (volatile int i = 0; i < 50000; i++)
      ;
  }
}

static void test_task_b(void) {
  while (1) {
    task_b_counter++;
    // Busy wait a bit
    for (volatile int i = 0; i < 50000; i++)
      ;
  }
}

static void test_task_c(void) {
  // This task counts to 500 then exits
  for (int i = 0; i < 500; i++) {
    task_c_counter++;
    for (volatile int j = 0; j < 50000; j++)
      ;
  }
  // Task will exit automatically when function returns
}

static void cmd_spawn(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
  printf("Creating test tasks...\n");

  // Reset counters
  task_a_counter = 0;
  task_b_counter = 0;
  task_c_counter = 0;

  // Create test tasks
  task_t *a = task_create("task_a", test_task_a);
  task_t *b = task_create("task_b", test_task_b);
  task_t *c = task_create("task_c", test_task_c);

  if (a && b && c) {
    printf("Tasks created successfully!\n");
    printf("Enabling preemptive multitasking...\n");
    task_enable();
    printf("Multitasking is now active.\n");
    printf("Watch the output - tasks A, B, C are running concurrently!\n");
    printf("Task C will exit after counting to 500.\n\n");
  } else {
    printf("Failed to create some tasks\n");
  }
}

// Callable from kernel for auto-testing
void test_spawn_tasks(void) {
  cmd_spawn(0, NULL);
}

// User mode test task entry points
// These functions run in Ring 3 and can only use syscalls to interact with kernel
// IMPORTANT: User tasks cannot access ANY global variables (they're in kernel memory)

// Helper to write a number - all data must be on stack (local variables)
static void user_write_num(char label, int val) {
  char buf[16];
  int pos = 0;
  buf[pos++] = '[';
  buf[pos++] = label;
  buf[pos++] = ':';
  // Convert number to string (simple approach)
  if (val >= 10000) buf[pos++] = '0' + (val / 10000) % 10;
  if (val >= 1000)  buf[pos++] = '0' + (val / 1000) % 10;
  if (val >= 100)   buf[pos++] = '0' + (val / 100) % 10;
  if (val >= 10)    buf[pos++] = '0' + (val / 10) % 10;
  buf[pos++] = '0' + val % 10;
  buf[pos++] = ']';
  buf[pos++] = ' ';
  sys_write(1, buf, pos);
}

// User task X - runs continuously (quiet mode)
static void user_task_x_entry(void) {
  int counter = 0;  // Local variable on user stack
  while (1) {
    counter++;
    // Busy wait
    for (volatile int i = 0; i < 30000; i++)
      ;
    // Yield occasionally
    if (counter % 25 == 0) {
      sys_yield();
    }
  }
}

// User task Y - runs for 300 iterations then exits (quiet mode)
static void user_task_y_entry(void) {
  int counter = 0;  // Local variable on user stack
  for (int i = 0; i < 300; i++) {
    counter++;
    for (volatile int j = 0; j < 30000; j++)
      ;
    if (counter % 25 == 0) {
      sys_yield();
    }
  }
  sys_exit(0);
}

// Simple user test task (original)
static void user_test_entry(void) {
  // Use syscalls to write to console
  const char *msg1 = "Hello from user mode!\n";
  sys_write(1, msg1, 22);

  // Loop a few times, yielding each iteration
  for (int i = 0; i < 5; i++) {
    const char *msg2 = "[User task running...]\n";
    sys_write(1, msg2, 23);

    // Yield to other tasks
    sys_yield();

    // Small delay
    for (volatile int j = 0; j < 1000000; j++)
      ;
  }

  const char *msg3 = "User task exiting via syscall\n";
  sys_write(1, msg3, 30);

  // Exit via syscall
  sys_exit(0);
}

static void cmd_usertest(int argc __attribute__((unused)),
                         char **argv __attribute__((unused))) {
  printf("Creating user-mode test task...\n");

  // Create user mode task
  task_t *user_task = task_create_user("user_test", user_test_entry);

  if (user_task) {
    printf("User task created successfully!\n");

    // Enable multitasking if not already enabled
    if (!task_is_enabled()) {
      printf("Enabling preemptive multitasking...\n");
      task_enable();
    }

    printf("User task is now scheduled to run in Ring 3.\n");
    printf("Watch for syscall messages from user mode!\n\n");
  } else {
    printf("Failed to create user task\n");
  }
}

// Combined demo: kernel threads (Ring 0) + user processes (Ring 3)
static void cmd_demo(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("=== mateOS Multitasking Demo ===\n");
  printf("Creating mixed Ring 0 (kernel) and Ring 3 (user) tasks...\n\n");

  // Reset kernel task counters
  task_a_counter = 0;
  task_b_counter = 0;
  task_c_counter = 0;

  // Create kernel-mode tasks (Ring 0)
  printf("Kernel tasks (Ring 0):\n");
  task_t *ka = task_create("kern_A", test_task_a);
  task_t *kb = task_create("kern_B", test_task_b);
  task_t *kc = task_create("kern_C", test_task_c);

  if (ka) printf("  - kern_A: infinite loop, prints [A:n]\n");
  if (kb) printf("  - kern_B: infinite loop, prints [B:n]\n");
  if (kc) printf("  - kern_C: counts to 500, prints [C:n], then exits\n");

  // Create user-mode tasks (Ring 3)
  printf("\nUser tasks (Ring 3):\n");
  task_t *ux = task_create_user("user_X", user_task_x_entry);
  task_t *uy = task_create_user("user_Y", user_task_y_entry);

  if (ux) printf("  - user_X: infinite loop, prints [X:n] via syscall\n");
  if (uy) printf("  - user_Y: counts to 300, prints [Y:n], then exits\n");

  // Check if all tasks were created
  int success = (ka && kb && kc && ux && uy);

  if (success) {
    printf("\nAll tasks created! Starting scheduler...\n");
    printf("Tasks C and Y will exit after completion.\n");
    printf("Use 'tasks' command to see running tasks.\n\n");

    task_enable();
  } else {
    printf("\nFailed to create some tasks!\n");
  }
}

static void cmd_ls(int argc __attribute__((unused)),
                   char **argv __attribute__((unused))) {
  ramfs_list();
}

// User task that execs hello.elf
static void exec_test_entry(void) {
  const char *msg = "About to exec hello.elf...\n";
  sys_write(1, msg, 28);

  // This should replace this task with hello.elf
  int ret = sys_exec("hello.elf");

  // Should only get here if exec failed
  const char *fail = "exec() failed!\n";
  sys_write(1, fail, 15);
  sys_exit(ret);
}

static void cmd_exec(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("Creating user task that will exec hello.elf...\n");

  task_t *exec_task = task_create_user("exec_test", exec_test_entry);

  if (!exec_task) {
    printf("Failed to create exec test task\n");
    return;
  }

  printf("Exec test task created\n");

  // Enable multitasking if not already enabled
  if (!task_is_enabled()) {
    task_enable();
  }
}

// User task that execs test.elf (comprehensive test suite)
// Note: String constants need to be in user-accessible memory
static char test_filename[] = "test.elf";
static void test_suite_entry(void) {
  const char *msg = "About to exec test.elf (comprehensive test suite)...\n";
  sys_write(1, msg, 54);

  // This should replace this task with test.elf
  int ret = sys_exec(test_filename);

  // Should only get here if exec failed
  const char *fail = "exec() failed!\n";
  sys_write(1, fail, 15);
  sys_exit(ret);
}

static void cmd_test(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("Running comprehensive userland test suite...\n");

  task_t *test_task = task_create_user("test_suite", test_suite_entry);

  if (!test_task) {
    printf("Failed to create test task\n");
    return;
  }

  printf("Test suite task created\n");

  // Enable multitasking if not already enabled
  if (!task_is_enabled()) {
    task_enable();
  }
}

// Parse command line into argc/argv
static int parse_command(char *line, char **argv, int max_args) {
  int argc = 0;
  char *p = line;

  while (*p && argc < max_args) {
    // Skip whitespace
    while (*p == ' ' || *p == '\t') {
      p++;
    }

    if (*p == '\0') {
      break;
    }

    // Start of argument
    argv[argc++] = p;

    // Find end of argument
    while (*p && *p != ' ' && *p != '\t') {
      p++;
    }

    if (*p) {
      *p = '\0';
      p++;
    }
  }

  return argc;
}

void console_init(void) {
  line_position = 0;
  line_buffer[0] = '\0';
  printf("\nWelcome to mateOS!\n");
  printf("Type 'help' for available commands.\n\n");
  print_prompt();
}

void console_putchar(char c) {
  if (c == '\n') {
    printf("\n");
  } else if (c == '\b') {
    // Backspace - move cursor back and erase
    printf("\b \b");
  } else {
    printf("%c", c);
  }
}

void console_handle_key(char c) {
  if (c == '\n') {
    // Execute command
    printf("\n");
    line_buffer[line_position] = '\0';

    if (line_position > 0) {
      console_execute_command(line_buffer);
    }

    // Reset line buffer
    line_position = 0;
    line_buffer[0] = '\0';
    print_prompt();

  } else if (c == '\b') {
    // Backspace
    if (line_position > 0) {
      line_position--;
      line_buffer[line_position] = '\0';
      console_putchar('\b');
    }

  } else if (c >= 32 && c < 127) {
    // Printable character
    if (line_position < CONSOLE_LINE_MAX - 1) {
      line_buffer[line_position++] = c;
      line_buffer[line_position] = '\0';
      console_putchar(c);
    }
  }
}

void console_execute_command(const char *line) {
  char line_copy[CONSOLE_LINE_MAX];
  char *argv[16];
  int argc;

  // Copy line so we can modify it
  size_t i;
  for (i = 0; i < CONSOLE_LINE_MAX - 1 && line[i]; i++) {
    line_copy[i] = line[i];
  }
  line_copy[i] = '\0';

  // Parse into argc/argv
  argc = parse_command(line_copy, argv, 16);

  if (argc == 0) {
    return;
  }

  // Find and execute command
  for (size_t i = 0; i < command_count; i++) {
    if (strcmp(argv[0], commands[i].name) == 0) {
      commands[i].handler(argc, argv);
      return;
    }
  }

  // Command not found
  printf("Unknown command: %s\n", argv[0]);
  printf("Type 'help' for available commands.\n");
}
