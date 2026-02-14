#include "syscalls.h"
#include "cmd_shared.h"
#include "libc.h"

// Wait for a keypress (blocking via yield loop)
static unsigned char waitkey(void) {
    unsigned char k;
    while (!(k = getkey(0))) {
        yield();
    }
    return k;
}

// Read a line from keyboard input
static int readline(char *buf, int max) {
    int pos = 0;
    while (1) {
        unsigned char key = waitkey();
        if (key == '\n') {
            print_char('\n');
            break;
        }
        if (key == '\b') {
            if (pos > 0) {
                pos--;
                print("\b \b");
            }
            continue;
        }
        if (key >= 32 && key < 127 && pos < max - 1) {
            buf[pos++] = (char)key;
            print_char((char)key);
        }
    }
    buf[pos] = '\0';
    return pos;
}

static void cmd_clear(void) {
    for (int i = 0; i < 25; i++) {
        print("\n");
    }
}

// ---- Background jobs tracking ----
#define MAX_BGJOBS 8

static struct {
    int pid;
    char name[32];
} bg_jobs[MAX_BGJOBS];
static int bg_count = 0;

static void bg_add(int pid, const char *name) {
    if (bg_count >= MAX_BGJOBS) return;
    bg_jobs[bg_count].pid = pid;
    int i;
    for (i = 0; name[i] && i < 31; i++)
        bg_jobs[bg_count].name[i] = name[i];
    bg_jobs[bg_count].name[i] = '\0';
    bg_count++;
}

// Check for finished background jobs and print notifications
static void bg_check(void) {
    int i = 0;
    while (i < bg_count) {
        int code = wait_nb(bg_jobs[i].pid);
        if (code != -1) {
            // Task finished
            print("[");
            print_num(bg_jobs[i].pid);
            print("] done  ");
            print(bg_jobs[i].name);
            if (code != 0) {
                print("  (exit ");
                print_num(code);
                print(")");
            }
            print("\n");
            // Remove from list by shifting
            for (int j = i; j < bg_count - 1; j++)
                bg_jobs[j] = bg_jobs[j + 1];
            bg_count--;
        } else {
            i++;
        }
    }
}

static void cmd_jobs(void) {
    if (bg_count == 0) {
        print("No background jobs\n");
        return;
    }
    for (int i = 0; i < bg_count; i++) {
        print("[");
        print_num(bg_jobs[i].pid);
        print("] running  ");
        print(bg_jobs[i].name);
        print("\n");
    }
}

// Parse a command line in-place into argv tokens (split on spaces).
// Returns argc. Modifies line by inserting NULs.
static int parse_argv(char *line, const char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        // Skip leading spaces
        while (*p == ' ') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        // Find end of token
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    print("mateOS shell v0.1\n");
    print("Type 'help' for commands.\n\n");

    char line[128];
    cmd_io_t io = {
        .print = print,
        .print_num = print_num,
        .clear = cmd_clear,
        .exit_help = "Exit shell"
    };

    while (1) {
        // Check for finished background jobs before showing prompt
        bg_check();

        print("$ ");
        int len = readline(line, sizeof(line));

        if (len == 0) continue;

        // Check for 'jobs' builtin
        if (len == 4 && line[0] == 'j' && line[1] == 'o' &&
            line[2] == 'b' && line[3] == 's') {
            bg_check();
            cmd_jobs();
            continue;
        }

        cmd_result_t builtin = cmd_try_builtin(line, &io);
        if (builtin == CMD_HANDLED) {
            continue;
        }
        if (builtin == CMD_EXIT) {
            print("Goodbye!\n");
            exit(0);
        }

        // Check for trailing '&' (background mode)
        int background = 0;
        if (len > 0 && line[len - 1] == '&') {
            background = 1;
            len--;
            line[len] = '\0';
            // Trim trailing whitespace
            while (len > 0 && line[len - 1] == ' ') {
                len--;
                line[len] = '\0';
            }
        }

        if (len == 0) continue;

        // Parse command line into argv tokens
        const char *args[16];
        int ac = parse_argv(line, args, 16);
        if (ac == 0) continue;

        // Auto-append .elf if not already present
        char elfname[64];
        const char *cmd = args[0];
        int cmdlen = strlen(cmd);
        if (cmdlen < 4 || cmd[cmdlen-4] != '.' || cmd[cmdlen-3] != 'e' ||
            cmd[cmdlen-2] != 'l' || cmd[cmdlen-1] != 'f') {
            // Copy and append .elf
            int i;
            for (i = 0; i < 59 && cmd[i]; i++) elfname[i] = cmd[i];
            elfname[i++] = '.'; elfname[i++] = 'e';
            elfname[i++] = 'l'; elfname[i++] = 'f'; elfname[i] = '\0';
            args[0] = elfname;
        }

        // Try to run as program
        int child = spawn_argv(args[0], args, ac);
        if (child >= 0) {
            if (background) {
                print("[");
                print_num(child);
                print("] ");
                print(args[0]);
                print("\n");
                bg_add(child, args[0]);
            } else {
                int code = wait(child);
                if (code != 0) {
                    print("[exited with code ");
                    print_num(code);
                    print("]\n");
                }
            }
        } else {
            print("Unknown command: ");
            print(args[0]);
            print("\n");
        }
    }
}
