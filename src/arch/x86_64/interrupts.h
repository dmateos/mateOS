/*
 * interrupts.h — x86_64 interrupt/exception types
 *
 * cpu_state_t matches the stack layout built by interrupts_asm.S.
 * Push order (RSP after SAVE_ALL = lowest address = r15):
 *   r15, r14, r13, r12, r11, r10, r9, r8,
 *   rdi, rsi, rbp, rbx, rdx, rcx, rax,
 *   error_code,
 *   rip, cs, rflags, rsp, ss   (CPU iret frame — always 5 words in 64-bit mode)
 */
#ifndef _ARCH_X86_64_INTERRUPTS_H
#define _ARCH_X86_64_INTERRUPTS_H

#include "lib.h"

typedef struct {
    /* Saved GPRs (SAVE_ALL pushes in this order; r15 at lowest addr) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Error code (0 if the exception has none) */
    uint64_t error_code;

    /* CPU iret frame (always 5 qwords in 64-bit mode) */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) cpu_state_t;

/* 64-bit IDT descriptor (16 bytes) */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

/* IRQ info — compatible layout with i686 for vfs_proc */
typedef struct {
    uint8_t  irq;
    uint8_t  vec;
    uint8_t  masked;
    uint8_t  has_handler;
    uint64_t handler_addr;
    const char *handler_name;
} irq_info_t;

/* C-level handlers called from assembly stubs */
void idt_exception_handler(cpu_state_t *state, uint64_t num);
void idt_irq_handler(uint64_t irq_num, uint64_t isr_num);

/* timer_handler_switch: saves context, runs scheduler, returns new RSP */
uint64_t timer_handler_switch(uint64_t rsp, int is_hw);

/* Common IRQ handler function type — use this in driver/subsystem code */
typedef void (*irq_handler_fn_t)(uint64_t irq, uint64_t vec);

/* Registration / query */
void init_idt(void);
void register_interrupt_handler_impl(uint8_t n, irq_handler_fn_t h,
                                     const char *name);
#define register_interrupt_handler(n, h) \
    register_interrupt_handler_impl((n), (h), #h)

void pic_unmask_irq(uint8_t irq);
void irq_list(void);
int  irq_get_snapshot(irq_info_t *out, int max);

#endif /* _ARCH_X86_64_INTERRUPTS_H */
