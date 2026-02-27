#include "cmd_shared.h"
#include "libc.h"
#include "syscalls.h"

static void cmd_help(const cmd_io_t *io) {
    io->print("Built-in commands:\n");
    io->print("  help    - Show this help\n");
    io->print("  clear   - Clear screen\n");
    io->print("  exit    - ");
    io->print(io->exit_help ? io->exit_help : "Exit");
    io->print("\n");
    io->print("  jobs    - List background jobs\n");
    io->print("\nRun any file by name (e.g. hello)\n");
    io->print("Append '&' to run in background (e.g. httpd &)\n");
}

cmd_result_t cmd_try_builtin(const char *line, const cmd_io_t *io) {
    if (strcmp(line, "help") == 0) {
        cmd_help(io);
        return CMD_HANDLED;
    }
    if (strcmp(line, "clear") == 0) {
        if (io->clear)
            io->clear();
        return CMD_HANDLED;
    }
    if (strcmp(line, "exit") == 0) {
        return CMD_EXIT;
    }
    return CMD_NOT_BUILTIN;
}
