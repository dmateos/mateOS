#include "syscalls.h"
#include "cmd_shared.h"

// Simple string helpers (no libc)
static int strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void print(const char *s) {
    write(1, s, strlen(s));
}

static void print_char(char c) {
    write(1, &c, 1);
}

// Print a decimal number
static void print_num(int n) {
    if (n < 0) {
        print_char('-');
        n = -n;
    }
    if (n == 0) {
        print_char('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        print_char(buf[--i]);
    }
}

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

void _start(void) {
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
        print("$ ");
        int len = readline(line, sizeof(line));

        if (len == 0) continue;

        cmd_result_t builtin = cmd_try_builtin(line, &io);
        if (builtin == CMD_HANDLED) {
            continue;
        }
        if (builtin == CMD_EXIT) {
            print("Goodbye!\n");
            exit(0);
        }

        // Try to run as program
        int child = spawn(line);
        if (child >= 0) {
            int code = wait(child);
            if (code != 0) {
                print("[exited with code ");
                print_num(code);
                print("]\n");
            }
        } else {
            print("Unknown command: ");
            print(line);
            print("\n");
        }
    }
}
