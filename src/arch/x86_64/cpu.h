#ifndef _ARCH_X86_64_CPU_H
#define _ARCH_X86_64_CPU_H

#include <stdint.h>

/* RFLAGS: IF=1 (bit 9) + reserved bit 1 */
#define ARCH_EFLAGS_DEFAULT 0x202u

static inline void cpu_halt(void) { __asm__ volatile("hlt"); }
static inline void cpu_enable_interrupts(void)  { __asm__ volatile("sti"); }
static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli"); }

static inline uint64_t cpu_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    return flags;
}

static inline void cpu_irq_restore(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static inline int cpu_interrupts_enabled(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

/* Software yield — kept as int 0x81 for now, SYSCALL path can be added later */
static inline void cpu_yield_interrupt(void) { __asm__ volatile("int $0x81"); }

static inline void cpu_shutdown(void) {
    /* QEMU ACPI poweroff — same port as i686 */
    __asm__ volatile("outw %w0, %w1" : : "a"((uint16_t)0x2000),
                                         "Nd"((uint16_t)0x604));
    cpu_disable_interrupts();
    while (1) cpu_halt();
}

#endif
