#ifndef _INTERRUPTS_H
#define _INTERRUPTS_H

#include "lib.h"

// CPU registers saved during context switch (kernel mode).
// Layout must match the pusha + iret frame built in interrupts_asm.S.
typedef struct {
    // Pushed by pusha
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // Ignored by popa
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    // Pushed by interrupt handler
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} __attribute__((packed)) cpu_state_t;

// Extended CPU state for user mode (includes user SS and ESP for ring
// transition)
typedef struct {
    // Pushed by pusha
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // Ignored by popa
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    // Pushed by CPU on interrupt from user mode
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp; // Only present on ring transition (user->kernel)
    uint32_t user_ss;  // Only present on ring transition (user->kernel)
} __attribute__((packed)) cpu_state_user_t;

// iret frame layout — what iret pops from the stack on ring transition
typedef struct {
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp; // Only present for ring transitions (user->kernel)
    uint32_t ss;  // Only present for ring transitions
} __attribute__((packed)) iret_frame_t;

typedef struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void register_interrupt_handler_impl(uint8_t, void (*h)(uint32_t, uint32_t),
                                     const char *name);
#define register_interrupt_handler(n, h)                                       \
    register_interrupt_handler_impl((n), (h), #h)
void init_idt(idt_ptr_t *idt_ptr, idt_entry_t *idt_entries);
void idt_breakpoint(void);
void idt_exception_handler(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t);
void idt_irq_handler(uint32_t, uint32_t);
void pic_unmask_irq(uint8_t irq);
void irq_list(void);
typedef struct {
    uint8_t irq;
    uint8_t vec;
    uint8_t masked;
    uint8_t has_handler;
    uint32_t handler_addr;
    const char *handler_name;
} irq_info_t;
int irq_get_snapshot(irq_info_t *out, int max);

extern void flush_idt(idt_ptr_t *idt_ptr);

// These are defined by the macro in idt_asm.s
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

// Task-switching timer handler
extern void irq0_task(void);

// Syscall handler (int 0x80)
extern void isr128(void);

// Software yield handler (int 0x81) - context switch without PIC EOI
extern void yield_task(void);

#endif
