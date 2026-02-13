#include "syscalls.h"

// Low-level syscall helpers.
static inline int __syscall0(unsigned int n) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "memory"
    );
    return ret;
}

static inline int __syscall1(unsigned int n, unsigned int a1) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int __syscall2(unsigned int n, unsigned int a1, unsigned int a2) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int __syscall3(unsigned int n, unsigned int a1, unsigned int a2,
                             unsigned int a3) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

int write(int fd, const void *buf, unsigned int len) {
    return __syscall3(SYS_WRITE, (unsigned int)fd, (unsigned int)buf, len);
}

void exit(int code) {
    (void)__syscall1(SYS_EXIT, (unsigned int)code);
    // Should never return, but loop just in case
    while (1) {
        __asm__ volatile("hlt");
    }
}

void yield(void) {
    (void)__syscall0(SYS_YIELD);
}

unsigned char *gfx_init(void) {
    return (unsigned char *)(unsigned int)__syscall0(SYS_GFX_INIT);
}

void gfx_exit(void) {
    (void)__syscall0(SYS_GFX_EXIT);
}

unsigned char getkey(unsigned int flags) {
    return (unsigned char)__syscall1(SYS_GETKEY, flags);
}

unsigned int gfx_info(void) {
    return (unsigned int)__syscall0(SYS_GFX_INFO);
}

int spawn(const char *filename) {
    return __syscall1(SYS_SPAWN, (unsigned int)filename);
}

int wait(int task_id) {
    return __syscall1(SYS_WAIT, (unsigned int)task_id);
}

int readdir(unsigned int index, char *buf, unsigned int size) {
    return __syscall3(SYS_READDIR, index, (unsigned int)buf, size);
}

int getpid(void) {
    return __syscall0(SYS_GETPID);
}

void taskinfo(void) {
    (void)__syscall0(SYS_TASKINFO);
}

void shutdown(void) {
    (void)__syscall0(SYS_SHUTDOWN);
}

int win_create(int width, int height, const char *title) {
    unsigned int packed = ((unsigned int)width << 16) | ((unsigned int)height & 0xFFFF);
    return __syscall2(SYS_WIN_CREATE, packed, (unsigned int)title);
}

int win_destroy(int wid) {
    return __syscall1(SYS_WIN_DESTROY, (unsigned int)wid);
}

int win_write(int wid, const unsigned char *data, unsigned int len) {
    return __syscall3(SYS_WIN_WRITE, (unsigned int)wid, (unsigned int)data, len);
}

int win_read(int wid, unsigned char *dest, unsigned int len) {
    return __syscall3(SYS_WIN_READ, (unsigned int)wid, (unsigned int)dest, len);
}

int win_getkey(int wid) {
    return __syscall1(SYS_WIN_GETKEY, (unsigned int)wid);
}

int win_sendkey(int wid, unsigned char key) {
    return __syscall2(SYS_WIN_SENDKEY, (unsigned int)wid, (unsigned int)key);
}

int wait_nb(int task_id) {
    return __syscall1(SYS_WAIT_NB, (unsigned int)task_id);
}

int tasklist(taskinfo_entry_t *buf, int max) {
    return __syscall2(SYS_TASKLIST, (unsigned int)buf, (unsigned int)max);
}

int win_list(win_info_t *out, int max_count) {
    return __syscall2(SYS_WIN_LIST, (unsigned int)out, (unsigned int)max_count);
}

int net_ping(unsigned int ip_be, unsigned int timeout_ms) {
    return __syscall2(SYS_PING, ip_be, timeout_ms);
}

void net_cfg(unsigned int ip_be, unsigned int mask_be, unsigned int gw_be) {
    (void)__syscall3(SYS_NETCFG, ip_be, mask_be, gw_be);
}

int net_get(unsigned int *ip_be, unsigned int *mask_be, unsigned int *gw_be) {
    return __syscall3(SYS_NETGET, (unsigned int)ip_be, (unsigned int)mask_be, (unsigned int)gw_be);
}

int sleep_ms(unsigned int ms) {
    return __syscall1(SYS_SLEEPMS, ms);
}

int sock_listen(unsigned int port) {
    return __syscall1(SYS_SOCK_LISTEN, port);
}

int sock_accept(int fd) {
    return __syscall1(SYS_SOCK_ACCEPT, (unsigned int)fd);
}

int sock_send(int fd, const void *buf, unsigned int len) {
    return __syscall3(SYS_SOCK_SEND, (unsigned int)fd, (unsigned int)buf, len);
}

int sock_recv(int fd, void *buf, unsigned int len) {
    return __syscall3(SYS_SOCK_RECV, (unsigned int)fd, (unsigned int)buf, len);
}

int sock_close(int fd) {
    return __syscall1(SYS_SOCK_CLOSE, (unsigned int)fd);
}

int win_read_text(int wid, char *buf, int max_len) {
    return __syscall3(SYS_WIN_READ_TEXT, (unsigned int)wid,
                      (unsigned int)buf, (unsigned int)max_len);
}

int win_set_stdout(int wid) {
    return __syscall1(SYS_WIN_SET_STDOUT, (unsigned int)wid);
}

int getmouse(int *x, int *y, unsigned char *buttons) {
    return __syscall3(SYS_GETMOUSE, (unsigned int)x,
                      (unsigned int)y, (unsigned int)buttons);
}
