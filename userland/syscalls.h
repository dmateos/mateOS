#ifndef _USERLAND_SYSCALLS_H
#define _USERLAND_SYSCALLS_H

// Syscall numbers (must match kernel)
#define SYS_WRITE    1
#define SYS_EXIT     2
#define SYS_YIELD    3
#define SYS_GFX_INIT 5
#define SYS_GFX_EXIT 6
#define SYS_GETKEY   7
#define SYS_SPAWN    8
#define SYS_WAIT     9
#define SYS_READDIR  10
#define SYS_GETPID   11
#define SYS_TASKINFO 12
#define SYS_SHUTDOWN 13

// Syscall wrappers
int write(int fd, const void *buf, unsigned int len);
void exit(int code) __attribute__((noreturn));
void yield(void);

// Graphics syscalls
unsigned char *gfx_init(void);
void gfx_exit(void);
unsigned char getkey(unsigned int flags);

// Process management syscalls
int spawn(const char *filename);
int wait(int task_id);
int readdir(unsigned int index, char *buf, unsigned int size);
int getpid(void);
void taskinfo(void);
void shutdown(void);

#endif
