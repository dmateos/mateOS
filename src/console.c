#include "console.h"
#include "lib.h"
#include "arch/i686/timer.h"
#include "liballoc/liballoc_1_1.h"
#include "ramfs.h"
#include "task.h"
#include "syscall.h"
#include "arch/i686/vga.h"
#include "arch/i686/io.h"
#include "arch/i686/paging.h"

// Console state
static char line_buffer[CONSOLE_LINE_MAX];
static size_t line_position = 0;

// Flag for graphics mode - keyboard handler checks this
static volatile int gfx_waiting = 0;
static volatile int gfx_key_pressed = 0;

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
static void cmd_gfx(int argc, char **argv);

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
    {"exec", "Run ELF binary from ramfs (usage: exec <filename>)", cmd_exec},
    {"gfx", "Switch to VGA Mode 13h graphics demo", cmd_gfx},
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
    for (volatile int i = 0; i < 50000; i++)
      ;
  }
}

static void test_task_b(void) {
  while (1) {
    task_b_counter++;
    for (volatile int i = 0; i < 50000; i++)
      ;
  }
}

static void test_task_c(void) {
  for (int i = 0; i < 500; i++) {
    task_c_counter++;
    for (volatile int j = 0; j < 50000; j++)
      ;
  }
}

static void cmd_spawn(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
  printf("Creating test tasks...\n");

  task_a_counter = 0;
  task_b_counter = 0;
  task_c_counter = 0;

  task_t *a = task_create("task_a", test_task_a);
  task_t *b = task_create("task_b", test_task_b);
  task_t *c = task_create("task_c", test_task_c);

  if (a && b && c) {
    printf("Tasks created successfully!\n");
    task_enable();
    printf("Multitasking is now active.\n");
    printf("Task C will exit after counting to 500.\n\n");
  } else {
    printf("Failed to create some tasks\n");
  }
}

// Callable from kernel for auto-testing
void test_spawn_tasks(void) {
  cmd_spawn(0, NULL);
}

// User mode task entry points for demo
static void user_task_x_entry(void) {
  int counter = 0;
  while (1) {
    counter++;
    for (volatile int i = 0; i < 30000; i++)
      ;
    if (counter % 25 == 0) {
      sys_yield();
    }
  }
}

static void user_task_y_entry(void) {
  int counter = 0;
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

static void user_test_entry(void) {
  const char *msg1 = "Hello from user mode!\n";
  sys_write(1, msg1, 22);

  for (int i = 0; i < 5; i++) {
    const char *msg2 = "[User task running...]\n";
    sys_write(1, msg2, 23);
    sys_yield();
    for (volatile int j = 0; j < 1000000; j++)
      ;
  }

  const char *msg3 = "User task exiting via syscall\n";
  sys_write(1, msg3, 30);
  sys_exit(0);
}

static void cmd_usertest(int argc __attribute__((unused)),
                         char **argv __attribute__((unused))) {
  printf("Creating user-mode test task...\n");

  task_t *user_task = task_create_user("user_test", user_test_entry);

  if (user_task) {
    printf("User task created successfully!\n");
    if (!task_is_enabled()) {
      task_enable();
    }
    printf("User task is now scheduled to run in Ring 3.\n\n");
  } else {
    printf("Failed to create user task\n");
  }
}

static void cmd_demo(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("=== mateOS Multitasking Demo ===\n");
  printf("Creating mixed Ring 0 (kernel) and Ring 3 (user) tasks...\n\n");

  task_a_counter = 0;
  task_b_counter = 0;
  task_c_counter = 0;

  task_t *ka = task_create("kern_A", test_task_a);
  task_t *kb = task_create("kern_B", test_task_b);
  task_t *kc = task_create("kern_C", test_task_c);

  if (ka) printf("  - kern_A: infinite loop\n");
  if (kb) printf("  - kern_B: infinite loop\n");
  if (kc) printf("  - kern_C: counts to 500, then exits\n");

  task_t *ux = task_create_user("user_X", user_task_x_entry);
  task_t *uy = task_create_user("user_Y", user_task_y_entry);

  if (ux) printf("  - user_X: infinite loop (Ring 3)\n");
  if (uy) printf("  - user_Y: counts to 300, then exits (Ring 3)\n");

  if (ka && kb && kc && ux && uy) {
    printf("\nAll tasks created! Starting scheduler...\n\n");
    task_enable();
  } else {
    printf("\nFailed to create some tasks!\n");
  }
}

static void cmd_ls(int argc __attribute__((unused)),
                   char **argv __attribute__((unused))) {
  ramfs_list();
}

// Generic exec: filename passed via static buffer so it's in kernel memory
// (accessible when the user task calls sys_exec)
static char exec_filename[64];

static void exec_entry(void) {
  sys_exec(exec_filename);
  sys_exit(1);
}

static void cmd_exec(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: exec <filename>\n");
    printf("Run an ELF binary from the ramfs.\n");
    printf("Use 'ls' to see available files.\n");
    return;
  }

  // Check file exists in ramfs
  ramfs_file_t *file = ramfs_lookup(argv[1]);
  if (!file) {
    printf("File not found: %s\n", argv[1]);
    return;
  }

  // Copy filename to static buffer
  size_t i;
  for (i = 0; i < sizeof(exec_filename) - 1 && argv[1][i]; i++) {
    exec_filename[i] = argv[1][i];
  }
  exec_filename[i] = '\0';

  // Mark exec_filename page as user-accessible so user task can pass it to syscall
  paging_set_user((uint32_t)exec_filename & ~0xFFF);

  printf("Loading %s...\n", exec_filename);

  task_t *t = task_create_user("exec", exec_entry);
  if (!t) {
    printf("Failed to create task\n");
    return;
  }

  if (!task_is_enabled()) {
    task_enable();
  }
}

// VGA Mode 13h graphics demo
static void cmd_gfx(int argc __attribute__((unused)),
                    char **argv __attribute__((unused))) {
  printf("Switching to VGA Mode 13h (320x200x256)...\n");

  // Enter graphics mode
  vga_enter_mode13h();

  // Clear to dark blue
  vga_clear(1);

  // Draw border
  vga_fill_rect(0, 0, VGA_WIDTH, 2, 15);           // Top
  vga_fill_rect(0, VGA_HEIGHT - 2, VGA_WIDTH, 2, 15); // Bottom
  vga_fill_rect(0, 0, 2, VGA_HEIGHT, 15);           // Left
  vga_fill_rect(VGA_WIDTH - 2, 0, 2, VGA_HEIGHT, 15); // Right

  // Title
  vga_draw_string(100, 10, "mateOS Graphics", 15);
  vga_draw_string(88, 24, "VGA Mode 13h - 320x200", 14);

  // Color palette display - show first 16 colors as squares
  vga_draw_string(10, 45, "Palette:", 15);
  for (int i = 0; i < 16; i++) {
    vga_fill_rect(10 + i * 18, 58, 16, 16, (uint8_t)i);
  }

  // Draw some shapes
  vga_draw_string(10, 85, "Shapes:", 15);

  // Filled rectangles
  vga_fill_rect(10, 98, 40, 30, 4);   // Red
  vga_fill_rect(60, 98, 40, 30, 2);   // Green
  vga_fill_rect(110, 98, 40, 30, 9);  // Light blue

  // Lines
  vga_draw_string(10, 135, "Lines:", 15);
  for (int i = 0; i < 8; i++) {
    vga_draw_line(10, 148 + i * 2, 150, 148 + 14 - i * 2,
                  (uint8_t)(8 + i));
  }

  // Diagonal cross
  vga_draw_line(200, 85, 300, 185, 12);  // Red diagonal
  vga_draw_line(300, 85, 200, 185, 10);  // Green diagonal

  // Instructions
  vga_draw_string(60, 188, "Press any key to return", 14);

  // Wait for keypress using interrupt-driven approach
  // The keyboard IRQ handler will set gfx_key_pressed when it sees a key
  // Note: cmd_gfx is called from inside the keyboard IRQ handler chain,
  // but the PIC EOI was already sent before our handler was called.
  // We need to re-enable CPU interrupts so new IRQs can be delivered.
  gfx_key_pressed = 0;
  gfx_waiting = 1;
  __asm__ volatile("sti");

  while (!gfx_key_pressed) {
    __asm__ volatile("hlt");  // Sleep until next interrupt
  }

  gfx_waiting = 0;

  // Return to text mode
  vga_enter_text_mode();

  printf("Returned to text mode.\n");
}

// Called from keyboard IRQ handler - returns 1 if key was consumed
int console_gfx_check_key(void) {
  if (gfx_waiting) {
    gfx_key_pressed = 1;
    return 1;  // Consumed, don't pass to console
  }
  return 0;
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
