#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            write(1, " ", 1);
        print(argv[i]);
    }
    write(1, "\n", 1);
    exit(0);
}
