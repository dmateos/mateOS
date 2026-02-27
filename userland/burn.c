#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("burn: busy loop started (Ctrl+C/kill to stop)\n");

    volatile unsigned int x = 0x12345678u;
    while (1) {
        // Keep ALU and branch units active so scheduler sees sustained CPU use.
        x ^= (x << 13);
        x ^= (x >> 17);
        x ^= (x << 5);
    }
}
