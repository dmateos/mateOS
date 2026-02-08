#ifndef _USERLAND_SYSCALLS_H
#define _USERLAND_SYSCALLS_H

// Syscall numbers (must match kernel)
#define SYS_WRITE    1
#define SYS_EXIT     2
#define SYS_YIELD    3
#define SYS_GFX_INIT 5
#define SYS_GFX_EXIT 6
#define SYS_GETKEY   7

// Syscall wrappers
int write(int fd, const void *buf, unsigned int len);
void exit(int code) __attribute__((noreturn));
void yield(void);

// Graphics syscalls
unsigned char *gfx_init(void);
void gfx_exit(void);
unsigned char getkey(unsigned int flags);

#endif
