#ifndef _USERLAND_SYSCALLS_H
#define _USERLAND_SYSCALLS_H

// Syscall numbers (must match kernel)
#define SYS_WRITE  1
#define SYS_EXIT   2
#define SYS_YIELD  3

// Syscall wrappers
int write(int fd, const void *buf, unsigned int len);
void exit(int code) __attribute__((noreturn));
void yield(void);

#endif
