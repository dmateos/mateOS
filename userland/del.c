#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("usage: del <file>\n");
        exit(1);
    }

    if (unlink(argv[1]) != 0) {
        print("del: failed: ");
        print(argv[1]);
        print("\n");
        exit(1);
    }

    exit(0);
}
