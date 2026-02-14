#include "syscalls.h"
#include "libc.h"

static int parse_pid(const char *s, int *out) {
    if (!s || !*s) return -1;
    int n = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        n = n * 10 + (s[i] - '0');
    }
    *out = n;
    return 0;
}

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("usage: kill <pid>\n");
        exit(1);
    }

    int pid = 0;
    if (parse_pid(argv[1], &pid) != 0) {
        print("kill: invalid pid\n");
        exit(1);
    }

    if (kill(pid) != 0) {
        print("kill: failed\n");
        exit(1);
    }

    exit(0);
}
