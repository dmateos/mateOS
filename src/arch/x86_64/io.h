#ifndef _ARCH_X86_64_IO_H
#define _ARCH_X86_64_IO_H

#include <stdint.h>

/* x86_64 I/O port access — identical instructions to i686 */

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline void io_wait(void) { outb(0x80, 0); }

/* Serial debug output (COM1) */
void serial_init(void);
void serial_putchar(char c);

#endif
