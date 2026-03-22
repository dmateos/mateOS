#ifndef _ARCH_X86_64_INTERRUPTS_H
#define _ARCH_X86_64_INTERRUPTS_H

#include "lib.h"

/*
 * 64-bit CPU state saved by interrupt stubs.
 * Layout must match the pushq sequence in interrupts_asm.S (to be written).
 *
 * On a 64-bit interrupt the CPU pushes (high→low on stack):
 *   SS, RSP, RFLAGS, CS, RIP   (always — no conditional ring-transition fields)
 *   error code                  (for exceptions that push one)
 * Then our stub pushes the GPRs.
 */
typedef struct {
    /* Saved by stub: pushq order (last pushed = lowest address) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp;
    uint64_t rdx, rcx, rbx, rax;

    /* Pushed by CPU on interrupt */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;   /* always saved in 64-bit mode */
    uint64_t ss;    /* always saved in 64-bit mode */
} __attribute__((packed)) cpu_state_t;

/* 64-bit IDT descriptor (16 bytes) */
typedef struct {
    uint16_t offset_low;   /* bits 0-15 of handler VA  */
    uint16_t selector;     /* kernel code segment       */
    uint8_t  ist;          /* IST index (0 = legacy stack) */
    uint8_t  flags;        /* type/DPL/present          */
    uint16_t offset_mid;   /* bits 16-31 of handler VA  */
    uint32_t offset_high;  /* bits 32-63 of handler VA  */
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

/* IRQ info — same layout as i686 for vfs_proc compat */
typedef struct {
    uint8_t  irq;
    uint8_t  vec;
    uint8_t  masked;
    uint8_t  has_handler;
    uint64_t handler_addr;
    const char *handler_name;
} irq_info_t;

void init_idt(void);
void register_interrupt_handler_impl(uint8_t n, void (*h)(uint64_t, uint64_t),
                                     const char *name);
#define register_interrupt_handler(n, h) \
    register_interrupt_handler_impl((n), (h), #h)

void pic_unmask_irq(uint8_t irq);
void irq_list(void);
int  irq_get_snapshot(irq_info_t *out, int max);

#endif
