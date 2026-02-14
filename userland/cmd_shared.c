#include "cmd_shared.h"
#include "syscalls.h"

static int sstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void cmd_help(const cmd_io_t *io) {
    io->print("Built-in commands:\n");
    io->print("  help    - Show this help\n");
    io->print("  clear   - Clear screen\n");
    io->print("  exit    - ");
    io->print(io->exit_help ? io->exit_help : "Exit");
    io->print("\n");
    io->print("  jobs    - List background jobs\n");
    io->print("\nRun any file by name (e.g. hello.elf)\n");
    io->print("Append '&' to run in background (e.g. httpd.elf &)\n");
}

cmd_result_t cmd_try_builtin(const char *line, const cmd_io_t *io) {
    if (sstrcmp(line, "help") == 0) {
        cmd_help(io);
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "clear") == 0) {
        if (io->clear) io->clear();
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "exit") == 0) {
        return CMD_EXIT;
    }
    return CMD_NOT_BUILTIN;
}
