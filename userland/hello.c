// Simple hello world program for mateOS userland

#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print("Hello from userland ELF!\n");
    print("This is a separate program loaded from initrd.\n");
    print("Exiting now...\n");
    exit(0);
}
