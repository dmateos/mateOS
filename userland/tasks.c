#include "syscalls.h"

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    write(1, s, slen(s));
}

static void print_num(int n) {
    if (n < 0) { write(1, "-", 1); n = -n; }
    if (n == 0) { write(1, "0", 1); return; }
    char buf[12];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) write(1, &buf[--i], 1);
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    taskinfo_entry_t tlist[16];
    int count = tasklist(tlist, 16);
    print("PID  State    Name\n");
    print("---  -------  ----\n");
    for (int i = 0; i < count; i++) {
        print_num((int)tlist[i].id);
        print("    ");
        switch (tlist[i].state) {
            case 0: print("ready  "); break;
            case 1: print("run    "); break;
            case 2: print("block  "); break;
            default: print("???    "); break;
        }
        print("  ");
        print(tlist[i].name);
        print("\n");
    }
    exit(0);
}
