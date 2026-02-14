#include "syscalls.h"

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    write(1, s, slen(s));
}

void _start(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) write(1, " ", 1);
        print(argv[i]);
    }
    write(1, "\n", 1);
    exit(0);
}
