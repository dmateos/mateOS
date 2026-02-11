#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "lib.h"

// System call numbers
#define SYS_WRITE    1   // write(fd, buf, len) - write to console
#define SYS_EXIT     2   // exit(code) - terminate current task
#define SYS_YIELD    3   // yield() - voluntarily give up CPU
#define SYS_EXEC     4   // exec(filename) - replace current process with ELF
#define SYS_GFX_INIT 5   // gfx_init() - enter Mode 13h, map framebuffer
#define SYS_GFX_EXIT 6   // gfx_exit() - return to text mode
#define SYS_GETKEY   7   // getkey(flags) - read key from buffer
#define SYS_SPAWN    8   // spawn(filename) - create child process from ELF
#define SYS_WAIT     9   // wait(task_id) - block until child exits
#define SYS_READDIR  10  // readdir(index, buf, size) - read ramfs directory entry
#define SYS_GETPID   11  // getpid() - get current task ID
#define SYS_TASKINFO 12  // taskinfo() - print task list to console
#define SYS_SHUTDOWN 13  // shutdown() - power off the machine
#define SYS_WIN_CREATE  14  // win_create(w_h_packed, title) -> wid
#define SYS_WIN_DESTROY 15  // win_destroy(wid)
#define SYS_WIN_WRITE   16  // win_write(wid, data, len) -> bytes
#define SYS_WIN_READ    17  // win_read(wid, dest, len) -> bytes
#define SYS_WIN_GETKEY  18  // win_getkey(wid) -> key
#define SYS_WIN_SENDKEY 19  // win_sendkey(wid, key)
#define SYS_WIN_LIST    20  // win_list(out, max_count) -> count
#define SYS_GFX_INFO   21  // gfx_info() -> (width<<16)|height
#define SYS_TASKLIST   22  // tasklist(buf, max) -> count of tasks filled
#define SYS_WAIT_NB    23  // wait_nb(task_id) -> exit code, or -1 if still running
#define SYS_PING       24  // ping(ip_be, timeout_ms) -> 0 ok, -1 timeout
#define SYS_NETCFG     25  // netcfg(ip_be, mask_be, gw_be)
#define SYS_NETGET     26  // netget(out_ip, out_mask, out_gw) -> 0
#define SYS_SLEEPMS    27  // sleepms(ms) -> 0

// Task info returned by SYS_TASKLIST
typedef struct {
  uint32_t id;
  uint32_t state;     // 0=ready, 1=running, 2=blocked, 3=terminated
  char name[32];
} taskinfo_entry_t;

// Initialize syscall handler (registers int 0x80)
void syscall_init(void);

// Load ELF from ramfs into a page directory. Returns entry point, or 0 on error.
struct page_directory;
uint32_t load_elf_into(struct page_directory *page_dir, const char *filename);

// Syscall handler called from assembly
// Arguments passed in registers: eax=syscall#, ebx=arg1, ecx=arg2, edx=arg3
// Returns result in eax
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx, void *frame);

// User-space syscall wrappers (inline assembly)
static inline int sys_write(int fd, const char *buf, size_t len) {
  int ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
    : "memory"
  );
  return ret;
}

static inline void sys_exit(int code) {
  __asm__ volatile(
    "int $0x80"
    :
    : "a"(SYS_EXIT), "b"(code)
  );
  // Should never return
  while (1) {}
}

static inline void sys_yield(void) {
  __asm__ volatile(
    "int $0x80"
    :
    : "a"(SYS_YIELD)
  );
}

static inline int sys_exec(const char *filename) {
  int ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_EXEC), "b"(filename)
  );
  return ret;
}

static inline uint32_t sys_gfx_init(void) {
  uint32_t ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_GFX_INIT)
    : "memory"
  );
  return ret;
}

static inline void sys_gfx_exit(void) {
  __asm__ volatile(
    "int $0x80"
    :
    : "a"(SYS_GFX_EXIT)
  );
}

static inline uint8_t sys_getkey(uint32_t flags) {
  uint32_t ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_GETKEY), "b"(flags)
    : "memory"
  );
  return (uint8_t)ret;
}

static inline int sys_spawn(const char *filename) {
  int ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_SPAWN), "b"(filename)
    : "memory"
  );
  return ret;
}

static inline int sys_wait(uint32_t task_id) {
  int ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_WAIT), "b"(task_id)
    : "memory"
  );
  return ret;
}

static inline int sys_readdir(uint32_t index, char *buf, uint32_t size) {
  int ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_READDIR), "b"(index), "c"(buf), "d"(size)
    : "memory"
  );
  return ret;
}

static inline int sys_getpid(void) {
  int ret;
  __asm__ volatile(
    "int $0x80"
    : "=a"(ret)
    : "a"(SYS_GETPID)
  );
  return ret;
}

#endif
