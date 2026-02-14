#include "syscalls.h"

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    write(1, "Powering off...\n", 16);
    shutdown();
    exit(0);
}
