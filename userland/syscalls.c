#include "syscalls.h"

// Inline assembly syscall wrappers

int write(int fd, const void *buf, unsigned int len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

void exit(int code) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_EXIT), "b"(code)
    );
    // Should never return, but loop just in case
    while (1) {
        __asm__ volatile("hlt");
    }
}

void yield(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_YIELD)
    );
}
