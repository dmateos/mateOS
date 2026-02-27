#ifndef _ARCH_I686_CPU_H
#define _ARCH_I686_CPU_H

#include <stdint.h>

static inline void cpu_halt(void) { __asm__ volatile("hlt"); }

static inline void cpu_enable_interrupts(void) { __asm__ volatile("sti"); }

static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli"); }

static inline uint32_t cpu_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags));
    return flags;
}

static inline void cpu_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

static inline int cpu_interrupts_enabled(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

static inline void cpu_yield_interrupt(void) { __asm__ volatile("int $0x81"); }

static inline void cpu_shutdown(void) {
    extern void outw(uint16_t port, uint16_t value);
    outw(0x604, 0x2000);
    cpu_disable_interrupts();
    while (1)
        cpu_halt();
}

#endif
