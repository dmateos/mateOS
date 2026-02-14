#include "syscalls.h"
#include "libc.h"

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    meminfo();
    exit(0);
}
