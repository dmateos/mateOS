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

// Initialize syscall handler (registers int 0x80)
void syscall_init(void);

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

#endif
