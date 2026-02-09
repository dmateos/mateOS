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
#define SYS_WIN_CREATE  14
#define SYS_WIN_DESTROY 15
#define SYS_WIN_WRITE   16
#define SYS_WIN_READ    17
#define SYS_WIN_GETKEY  18
#define SYS_WIN_SENDKEY 19
#define SYS_WIN_LIST    20

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

// Window info struct (must match kernel's win_info_t)
typedef struct {
    int window_id;
    unsigned int owner_pid;
    int w, h;
    char title[32];
} win_info_t;

// Window management syscalls
int win_create(int width, int height, const char *title);
int win_destroy(int wid);
int win_write(int wid, const unsigned char *data, unsigned int len);
int win_read(int wid, unsigned char *dest, unsigned int len);
int win_getkey(int wid);
int win_sendkey(int wid, unsigned char key);
int win_list(win_info_t *out, int max_count);

#endif
