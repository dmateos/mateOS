#include "console.h"
#include "lib.h"
#include "arch/i686/timer.h"

// Console state
static char line_buffer[CONSOLE_LINE_MAX];
static size_t line_position = 0;

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

// Command table
static const command_t commands[] = {
    {"help", "Show this help message", cmd_help},
    {"clear", "Clear the screen", cmd_clear},
    {"echo", "Print arguments", cmd_echo},
    {"uptime", "Show system uptime", cmd_uptime},
    {"reboot", "Reboot the system", cmd_reboot},
};

static const size_t command_count = sizeof(commands) / sizeof(commands[0]);

// Print the prompt
static void print_prompt(void) { printf("mateOS> "); }

// Command implementations
static void cmd_help(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
  printf("Available commands:\n");
  for (size_t i = 0; i < command_count; i++) {
    printf("  %-10s - %s\n", commands[i].name, commands[i].description);
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

  printf("Uptime: %d:%02d:%02d (%d seconds, %d ticks)\n", hours, minutes, secs,
         seconds, ticks);
}

static void cmd_reboot(int argc __attribute__((unused)),
                       char **argv __attribute__((unused))) {
  printf("Rebooting...\n");
  // Wait a moment for the message to display
  for (volatile int i = 0; i < 10000000; i++)
    ;

  // Reboot via keyboard controller
  __asm__ volatile("movb $0xFE, %al\n"
                   "outb %al, $0x64\n");

  // If that didn't work, triple fault
  __asm__ volatile("lidt 0");
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
