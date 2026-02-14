// Simple hello world program for mateOS userland

#include "syscalls.h"

// Helper to write a string
static void print(const char *str) {
    const char *p = str;
    int len = 0;
    while (*p++) len++;
    write(1, str, len);
}

void _start(int argc, char **argv) {
  (void)argc; (void)argv;
    print("Hello from userland ELF!\n");
    print("This is a separate program loaded from initrd.\n");
    print("Exiting now...\n");
    exit(0);
}
