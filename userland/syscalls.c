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

unsigned int gfx_info(void) {
    unsigned int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GFX_INFO)
    );
    return ret;
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

int win_create(int width, int height, const char *title) {
    unsigned int packed = ((unsigned int)width << 16) | ((unsigned int)height & 0xFFFF);
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_CREATE), "b"(packed), "c"(title)
        : "memory"
    );
    return ret;
}

int win_destroy(int wid) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_DESTROY), "b"(wid)
        : "memory"
    );
    return ret;
}

int win_write(int wid, const unsigned char *data, unsigned int len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_WRITE), "b"(wid), "c"(data), "d"(len)
        : "memory"
    );
    return ret;
}

int win_read(int wid, unsigned char *dest, unsigned int len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_READ), "b"(wid), "c"(dest), "d"(len)
        : "memory"
    );
    return ret;
}

int win_getkey(int wid) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_GETKEY), "b"(wid)
        : "memory"
    );
    return ret;
}

int win_sendkey(int wid, unsigned char key) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_SENDKEY), "b"(wid), "c"(key)
        : "memory"
    );
    return ret;
}

int wait_nb(int task_id) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WAIT_NB), "b"(task_id)
        : "memory"
    );
    return ret;
}

int tasklist(taskinfo_entry_t *buf, int max) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_TASKLIST), "b"(buf), "c"(max)
        : "memory"
    );
    return ret;
}

int win_list(win_info_t *out, int max_count) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WIN_LIST), "b"(out), "c"(max_count)
        : "memory"
    );
    return ret;
}

int net_ping(unsigned int ip_be, unsigned int timeout_ms) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_PING), "b"(ip_be), "c"(timeout_ms)
        : "memory"
    );
    return ret;
}

void net_cfg(unsigned int ip_be, unsigned int mask_be, unsigned int gw_be) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_NETCFG), "b"(ip_be), "c"(mask_be), "d"(gw_be)
        : "memory"
    );
}

int net_get(unsigned int *ip_be, unsigned int *mask_be, unsigned int *gw_be) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_NETGET), "b"(ip_be), "c"(mask_be), "d"(gw_be)
        : "memory"
    );
    return ret;
}

int sleep_ms(unsigned int ms) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SLEEPMS), "b"(ms)
        : "memory"
    );
    return ret;
}
