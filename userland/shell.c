#include "syscalls.h"

// Simple string helpers (no libc)
static int strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
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

// Built-in: help
static void cmd_help(void) {
    print("Built-in commands:\n");
    print("  help    - Show this help\n");
    print("  ls      - List files in ramfs\n");
    print("  tasks   - Show running tasks\n");
    print("  echo    - Print arguments\n");
    print("  clear   - Clear screen\n");
    print("  shutdown- Power off\n");
    print("  exit    - Exit shell\n");
    print("\nRun any file by name (e.g. 'hello.elf')\n");
}

// Built-in: ls
static void cmd_ls(void) {
    char name[32];
    unsigned int i = 0;
    while (readdir(i, name, sizeof(name)) > 0) {
        print("  ");
        print(name);
        print("\n");
        i++;
    }
    if (i == 0) {
        print("  (no files)\n");
    }
}

// Built-in: echo
static void cmd_echo(const char *line) {
    // Skip "echo " prefix
    if (line[4] == ' ') {
        print(line + 5);
    }
    print("\n");
}

// Built-in: clear
static void cmd_clear(void) {
    for (int i = 0; i < 25; i++) {
        print("\n");
    }
}

void _start(void) {
    print("mateOS shell v0.1\n");
    print("Type 'help' for commands.\n\n");

    char line[128];

    while (1) {
        print("$ ");
        int len = readline(line, sizeof(line));

        if (len == 0) continue;

        // Check built-in commands
        if (strcmp(line, "help") == 0) {
            cmd_help();
        } else if (strcmp(line, "ls") == 0) {
            cmd_ls();
        } else if (strcmp(line, "tasks") == 0) {
            taskinfo_entry_t tlist[16];
            int count = tasklist(tlist, 16);
            print("PID  State    Name\n");
            print("---  -------  ----\n");
            for (int ti = 0; ti < count; ti++) {
                print_num((int)tlist[ti].id);
                print("    ");
                switch (tlist[ti].state) {
                    case 0: print("ready  "); break;
                    case 1: print("run    "); break;
                    case 2: print("block  "); break;
                    default: print("???    "); break;
                }
                print("  ");
                print(tlist[ti].name);
                print("\n");
            }
        } else if (strncmp(line, "echo ", 5) == 0 || strcmp(line, "echo") == 0) {
            if (len > 4) {
                cmd_echo(line);
            } else {
                print("\n");
            }
        } else if (strcmp(line, "clear") == 0) {
            cmd_clear();
        } else if (strcmp(line, "shutdown") == 0) {
            print("Powering off...\n");
            shutdown();
        } else if (strcmp(line, "exit") == 0) {
            print("Goodbye!\n");
            exit(0);
        } else {
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
}
