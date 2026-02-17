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
#define SYS_GFX_INFO   21
#define SYS_TASKLIST   22
#define SYS_WAIT_NB    23
#define SYS_PING       24
#define SYS_NETCFG     25
#define SYS_NETGET     26
#define SYS_SLEEPMS    27
#define SYS_SOCK_LISTEN 28
#define SYS_SOCK_ACCEPT 29
#define SYS_SOCK_SEND   30
#define SYS_SOCK_RECV   31
#define SYS_SOCK_CLOSE  32
#define SYS_WIN_READ_TEXT  33
#define SYS_WIN_SET_STDOUT 34
#define SYS_GETMOUSE       35
#define SYS_OPEN           36
#define SYS_FREAD          37
#define SYS_FWRITE         38
#define SYS_CLOSE          39
#define SYS_SEEK           40
#define SYS_STAT           41
#define SYS_DETACH         42
#define SYS_UNLINK         43
#define SYS_KILL           44
#define SYS_GETTICKS       45
#define SYS_NETSTATS       50
#define SYS_SBRK           51

// Syscall wrappers
int write(int fd, const void *buf, unsigned int len);
void exit(int code) __attribute__((noreturn));
void yield(void);

// Graphics syscalls
unsigned char *gfx_init(void);
void gfx_exit(void);
unsigned char getkey(unsigned int flags);
unsigned int gfx_info(void);  // Returns (width << 16) | height

// Process management syscalls
int spawn(const char *filename);
int spawn_argv(const char *filename, const char **argv, int argc);
int wait(int task_id);
int readdir(unsigned int index, char *buf, unsigned int size);
int getpid(void);
void taskinfo(void);
void shutdown(void);

// Task info entry (must match kernel's taskinfo_entry_t)
typedef struct {
    unsigned int id;
    unsigned int parent_id;
    unsigned int ring;     // 0=kernel, 3=user
    unsigned int state;    // 0=ready, 1=running, 2=blocked, 3=terminated
    unsigned int runtime_ticks;
    char name[32];
} taskinfo_entry_t;

int tasklist(taskinfo_entry_t *buf, int max);
int wait_nb(int task_id);  // Non-blocking: returns -1 if still running

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
int net_ping(unsigned int ip_be, unsigned int timeout_ms);
void net_cfg(unsigned int ip_be, unsigned int mask_be, unsigned int gw_be);
int net_get(unsigned int *ip_be, unsigned int *mask_be, unsigned int *gw_be);
int net_stats(unsigned int *rx_packets, unsigned int *tx_packets);
int sleep_ms(unsigned int ms);

// TCP socket syscalls
int sock_listen(unsigned int port);
int sock_accept(int fd);
int sock_send(int fd, const void *buf, unsigned int len);
int sock_recv(int fd, void *buf, unsigned int len);
int sock_close(int fd);

// Window stdout redirection
int win_read_text(int wid, char *buf, int max_len);
int win_set_stdout(int wid);

// Mouse
int getmouse(int *x, int *y, unsigned char *buttons);

// Seek whence constants
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Special key codes
#define KEY_LEFT   0x80
#define KEY_RIGHT  0x81
#define KEY_UP     0x82
#define KEY_DOWN   0x83

// Open flags
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  4
#define O_TRUNC  8

// Stat result (must match kernel's vfs_stat_t)
typedef struct {
    unsigned int size;
    unsigned int type;  // 0=file, 1=dir
} stat_t;

// File I/O syscalls
int open(const char *path, int flags);
int fread(int fd, void *buf, unsigned int len);
int fwrite(int fd, const void *buf, unsigned int len);
int close(int fd);
int seek(int fd, int offset, int whence);
int stat(const char *path, stat_t *st);
int unlink(const char *path);
int kill(int task_id);
unsigned int get_ticks(void);
void *sbrk(int increment);

// Process detach (for GUI apps)
int detach(void);

#endif
