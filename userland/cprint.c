#include "syscalls.h"

static int raw_write(int fd, const void *buf, unsigned int len) {
    unsigned int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return (int)ret;
}

// SmallerC currently references this symbol directly.
void smallerc_print(const char *s) __asm__("$print");
void smallerc_print(const char *s) {
    unsigned int n = 0;
    if (!s) return;
    while (s[n]) n++;
    if (n) raw_write(1, s, n);
}
