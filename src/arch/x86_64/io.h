/*
 * io.h — x86_64 I/O port access and serial port
 *
 * Identical port instructions to i686.  Declared as extern (non-inline)
 * so that drivers which include arch/i686/io.h (which also declares them
 * as extern) can link against the same symbols.
 *
 * Implementations are in io.c.
 */
#ifndef _ARCH_X86_64_IO_H
#define _ARCH_X86_64_IO_H

#include "lib.h"

/* I/O port access — same ports/instructions as i686 */
uint8_t  inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
void     outb(uint16_t port, uint8_t  value);
void     outw(uint16_t port, uint16_t value);
void     outl(uint16_t port, uint32_t value);

static inline void io_wait(void) { outb(0x80, 0); }

/* Serial debug output (COM1) */
void serial_init(void);
void serial_putchar(char c);

/* MSR access (not in i686 io.h but useful for x86_64) */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" :
                     : "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

/* QEMU debug exit port (same as i686) */
#define QEMU_DEBUG_EXIT_PORT 0xf4

#endif /* _ARCH_X86_64_IO_H */
