/*
 * idt.c — 64-bit IDT setup
 *
 * Installs handlers for:
 *   0–31   CPU exceptions (isr0..isr31 from interrupts_asm.S)
 *   32–47  Hardware IRQs (irq0..irq15, after PIC remap to base 32)
 *   0x80   Syscall (int 0x80, DPL=3 so user mode can invoke it)
 */
#include "arch/x86_64/idt.h"
#include "arch/x86_64/gdt.h"
#include "lib.h"

static idt_entry64_t idt64[256];

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr64_t;

static idtr64_t idtr64;

/* External ISR stubs from interrupts_asm.S */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void irq0_task(void);  /* timer with context switch */
extern void yield_task(void); /* software yield (int 0x20) */
extern void isr128(void);     /* syscall (int 0x80) */

void idt64_set_gate(uint8_t num, uintptr_t handler, uint8_t ist, uint8_t dpl) {
    idt_entry64_t *e = &idt64[num];
    e->offset_lo  = handler & 0xFFFF;
    e->cs         = GDT64_KERNEL_CODE;
    e->ist        = ist & 0x7;
    /* type = 0xE (interrupt gate, clears IF), P=1, DPL=dpl */
    e->type_attr  = 0x8E | ((dpl & 3) << 5);
    e->offset_mid = (handler >> 16) & 0xFFFF;
    e->offset_hi  = (handler >> 32) & 0xFFFFFFFF;
    e->reserved   = 0;
}

void idt64_init(void) {
    /* CPU exceptions */
    idt64_set_gate(0,  (uintptr_t)isr0,  0, 0);
    idt64_set_gate(1,  (uintptr_t)isr1,  0, 0);
    idt64_set_gate(2,  (uintptr_t)isr2,  0, 0);
    idt64_set_gate(3,  (uintptr_t)isr3,  0, 3);   /* breakpoint — DPL=3 */
    idt64_set_gate(4,  (uintptr_t)isr4,  0, 0);
    idt64_set_gate(5,  (uintptr_t)isr5,  0, 0);
    idt64_set_gate(6,  (uintptr_t)isr6,  0, 0);
    idt64_set_gate(7,  (uintptr_t)isr7,  0, 0);
    idt64_set_gate(8,  (uintptr_t)isr8,  0, 0);   /* double fault — could use IST */
    idt64_set_gate(9,  (uintptr_t)isr9,  0, 0);
    idt64_set_gate(10, (uintptr_t)isr10, 0, 0);
    idt64_set_gate(11, (uintptr_t)isr11, 0, 0);
    idt64_set_gate(12, (uintptr_t)isr12, 0, 0);
    idt64_set_gate(13, (uintptr_t)isr13, 0, 0);
    idt64_set_gate(14, (uintptr_t)isr14, 0, 0);
    idt64_set_gate(15, (uintptr_t)isr15, 0, 0);
    idt64_set_gate(16, (uintptr_t)isr16, 0, 0);
    idt64_set_gate(17, (uintptr_t)isr17, 0, 0);
    idt64_set_gate(18, (uintptr_t)isr18, 0, 0);
    idt64_set_gate(19, (uintptr_t)isr19, 0, 0);
    idt64_set_gate(20, (uintptr_t)isr20, 0, 0);
    idt64_set_gate(21, (uintptr_t)isr21, 0, 0);
    idt64_set_gate(22, (uintptr_t)isr22, 0, 0);
    idt64_set_gate(23, (uintptr_t)isr23, 0, 0);
    idt64_set_gate(24, (uintptr_t)isr24, 0, 0);
    idt64_set_gate(25, (uintptr_t)isr25, 0, 0);
    idt64_set_gate(26, (uintptr_t)isr26, 0, 0);
    idt64_set_gate(27, (uintptr_t)isr27, 0, 0);
    idt64_set_gate(28, (uintptr_t)isr28, 0, 0);
    idt64_set_gate(29, (uintptr_t)isr29, 0, 0);
    idt64_set_gate(30, (uintptr_t)isr30, 0, 0);
    idt64_set_gate(31, (uintptr_t)isr31, 0, 0);

    /* Hardware IRQs (PIC remapped to 32–47) */
    idt64_set_gate(32, (uintptr_t)irq0_task, 0, 0);  /* timer — with context switch */
    idt64_set_gate(33, (uintptr_t)irq1,      0, 0);  /* keyboard */
    idt64_set_gate(34, (uintptr_t)irq2,      0, 0);
    idt64_set_gate(35, (uintptr_t)irq3,      0, 0);
    idt64_set_gate(36, (uintptr_t)irq4,      0, 0);
    idt64_set_gate(37, (uintptr_t)irq5,      0, 0);
    idt64_set_gate(38, (uintptr_t)irq6,      0, 0);
    idt64_set_gate(39, (uintptr_t)irq7,      0, 0);
    idt64_set_gate(40, (uintptr_t)irq8,      0, 0);
    idt64_set_gate(41, (uintptr_t)irq9,      0, 0);
    idt64_set_gate(42, (uintptr_t)irq10,     0, 0);
    idt64_set_gate(43, (uintptr_t)irq11,     0, 0);
    idt64_set_gate(44, (uintptr_t)irq12,     0, 0);  /* mouse */
    idt64_set_gate(45, (uintptr_t)irq13,     0, 0);
    idt64_set_gate(46, (uintptr_t)irq14,     0, 0);  /* ATA primary */
    idt64_set_gate(47, (uintptr_t)irq15,     0, 0);  /* ATA secondary */

    /* Software yield via int 0x20 */
    idt64_set_gate(0x20, (uintptr_t)yield_task, 0, 3);

    /* Syscall via int 0x80 — DPL=3 so user mode can call it */
    idt64_set_gate(0x80, (uintptr_t)isr128, 0, 3);

    idtr64.limit = sizeof(idt64) - 1;
    idtr64.base  = (uint64_t)&idt64;

    __asm__ volatile("lidt %0" :: "m"(idtr64));
    __asm__ volatile("sti");
}
