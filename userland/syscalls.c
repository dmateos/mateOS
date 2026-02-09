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

unsigned char *gfx_init(void) {
    unsigned int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GFX_INIT)
        : "memory"
    );
    return (unsigned char *)ret;
}

void gfx_exit(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_GFX_EXIT)
    );
}

unsigned char getkey(unsigned int flags) {
    unsigned int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETKEY), "b"(flags)
        : "memory"
    );
    return (unsigned char)ret;
}

int spawn(const char *filename) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SPAWN), "b"(filename)
        : "memory"
    );
    return ret;
}

int wait(int task_id) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WAIT), "b"(task_id)
        : "memory"
    );
    return ret;
}

int readdir(unsigned int index, char *buf, unsigned int size) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_READDIR), "b"(index), "c"(buf), "d"(size)
        : "memory"
    );
    return ret;
}

int getpid(void) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETPID)
    );
    return ret;
}

void taskinfo(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_TASKINFO)
    );
}

void shutdown(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_SHUTDOWN)
    );
}
