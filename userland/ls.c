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
    (void)argc; (void)argv;
    char name[32];
    unsigned int i = 0;
    while (readdir(i, name, sizeof(name)) > 0) {
        print("  ");
        print(name);
        print("\n");
        i++;
    }
    if (i == 0) print("  (no files)\n");
    exit(0);
}
