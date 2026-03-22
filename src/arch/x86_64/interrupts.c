/*
 * interrupts.c — x86_64 interrupt/exception C-level handler
 *
 * Provides:
 *   idt_exception_handler — called by exception stubs (isr0..isr31)
 *   idt_irq_handler       — called by generic IRQ stubs (irq1..irq15)
 *   timer_handler_switch  — called by irq0_task and yield_task with context switch
 *   init_idt              — sets up PIC + calls idt64_init()
 *
 * Mirrors the i686 interrupts.c interface so the rest of the kernel (task.c,
 * keyboard.c, rtl8139, ata_pio) doesn't need to change.
 */
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"
#include "proc/task.h"
#include "lib.h"
#include "memlayout.h"

/* ── PIC ─────────────────────────────────────────────────────────────── */

#define MASTER_PIC_COMMAND 0x20
#define MASTER_PIC_DATA    0x21
#define SLAVE_PIC_COMMAND  0xA0
#define SLAVE_PIC_DATA     0xA1

static void pic_remap(void) {
    outb(MASTER_PIC_COMMAND, 0x11);
    outb(SLAVE_PIC_COMMAND,  0x11);
    outb(MASTER_PIC_DATA,    0x20);  /* master base: IRQ0 → vector 32 */
    outb(SLAVE_PIC_DATA,     0x28);  /* slave  base: IRQ8 → vector 40 */
    outb(MASTER_PIC_DATA,    0x04);
    outb(SLAVE_PIC_DATA,     0x02);
    outb(MASTER_PIC_DATA,    0x01);
    outb(SLAVE_PIC_DATA,     0x01);
    outb(MASTER_PIC_DATA,    0x00);  /* unmask all */
    outb(SLAVE_PIC_DATA,     0x00);
}

static void pic_ack(int irq) {
    if (irq >= 8)
        outb(SLAVE_PIC_COMMAND, 0x20);
    outb(MASTER_PIC_COMMAND, 0x20);
}

void pic_unmask_irq(uint8_t irq) {
    if (irq < 8) {
        uint8_t m = inb(MASTER_PIC_DATA);
        outb(MASTER_PIC_DATA, m & (uint8_t)~(1 << irq));
    } else {
        uint8_t m = inb(SLAVE_PIC_DATA);
        outb(SLAVE_PIC_DATA, m & (uint8_t)~(1 << (irq - 8)));
        uint8_t mm = inb(MASTER_PIC_DATA);
        outb(MASTER_PIC_DATA, mm & (uint8_t)~(1 << 2));
    }
}

/* ── IRQ handler table ────────────────────────────────────────────────── */

static irq_handler_fn_t irq_handlers[256];
static const char *irq_names[256];

void register_interrupt_handler_impl(uint8_t n, irq_handler_fn_t h,
                                     const char *name) {
    irq_handlers[n] = h;
    irq_names[n]    = name;
}

/* ── Exception names ──────────────────────────────────────────────────── */

static const char *exc_names[] = {
    "Division Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Reserved",
    "FPU Error", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security", "Reserved",
};

/* ── Exception handler ────────────────────────────────────────────────── */

void idt_exception_handler(cpu_state_t *state, uint64_t num) {
    if (num == 14) {
        /* Page fault */
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("[x86_64] #PF at RIP=0x%x CR2=0x%x err=0x%x\n",
                (uint32_t)state->rip, (uint32_t)cr2,
                (uint32_t)state->error_code);
    } else {
        kprintf("[x86_64] Exception %d (%s) at RIP=0x%x err=0x%x\n",
                (int)num,
                num < 32 ? exc_names[num] : "?",
                (uint32_t)state->rip,
                (uint32_t)state->error_code);
    }

    /* If the fault came from user mode (CS ring bits set), kill the task */
    if ((state->cs & 3) == 3) {
        kprintf("[x86_64] user-mode exception — killing task\n");
        task_exit_with_code(-(int)num);
        /* task_exit_with_code yields to next task, never returns here */
    }

    /* Kernel fault — halt */
    kprintf("[x86_64] kernel exception — halting\n");
    __asm__ volatile("cli; hlt");
    while (1) {}
}

/* ── Generic IRQ handler ──────────────────────────────────────────────── */

void idt_irq_handler(uint64_t irq_num, uint64_t isr_num) {
    pic_ack((int)irq_num);
    if (irq_handlers[isr_num]) {
        irq_handler_fn_t h = irq_handlers[isr_num];
        h((uintptr_t)irq_num, (uintptr_t)isr_num);
    }
}

/* ── IRQ info for /mos/kirq ────────────────────────────────────────────── */

int irq_get_snapshot(irq_info_t *out, int max) {
    int count = 0;
    for (int i = 0; i < 256 && count < max; i++) {
        if (!irq_handlers[i] && !irq_names[i]) continue;
        out[count].irq          = (uint8_t)(i >= 32 && i < 48 ? i - 32 : 0);
        out[count].vec          = (uint8_t)i;
        out[count].masked       = 0;
        out[count].has_handler  = irq_handlers[i] ? 1 : 0;
        out[count].handler_addr = (uint64_t)(uintptr_t)irq_handlers[i];
        out[count].handler_name = irq_names[i];
        count++;
    }
    return count;
}

void irq_list(void) {
    kprintf("IRQ handlers:\n");
    for (int i = 0; i < 256; i++) {
        if (irq_handlers[i])
            kprintf("  vec %3d: %s\n", i,
                    irq_names[i] ? irq_names[i] : "?");
    }
}

/* ── init_idt — public entry point ───────────────────────────────────── */

void init_idt(void) {
    pic_remap();
    idt64_init();
    kprintf("[idt64] initialized\n");
}
