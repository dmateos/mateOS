/*
 * timer.c — x86_64 PIT timer implementation
 *
 * Mirrors the i686 timer.c interface.  The PIT (8253/8254) is at the same
 * I/O ports on x86_64 as on i686.
 *
 * timer_handler_switch() is called from irq0_task in interrupts_asm.S:
 *   rdi = saved RSP (pointer to cpu_state_t on kernel stack)
 *   rsi = is_hw (1 for real IRQ, 0 for software yield)
 *   returns new RSP in rax
 */
#include "arch/x86_64/timer.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/io.h"
#include "proc/task.h"
#include "lib.h"

#define MASTER_PIC_COMMAND 0x20
#define MASTER_PIC_DATA    0x21

static volatile uint32_t system_ticks  = 0;
static uint32_t          timer_freq    = 0;

/* Called from irq0_task (is_hw=1) and yield_task (is_hw=0).
 * rsp = saved kernel stack pointer (points to cpu_state_t).
 * Returns the new task's RSP. */
uint64_t timer_handler_switch(uint64_t rsp, int is_hw) {
    if (is_hw) {
        system_ticks++;
        /* ACK IRQ0 */
        outb(MASTER_PIC_COMMAND, 0x20);
    }

    /* Save current task's stack pointer */
    task_t *cur = task_current();
    if (cur)
        cur->stack_top = (uint32_t *)rsp;

    /* Run scheduler if multitasking is active */
    if (task_is_enabled()) {
        /* schedule() is still declared as (uint32_t *, uint32_t) for i686.
         * On x86_64 we cast — safe because pointers are 64-bit but the
         * scheduler only stores/returns the pointer value, not dereferencing
         * it as 32-bit.  A proper 64-bit scheduler refactor is a TODO. */
        uint32_t *new_esp = schedule((uint32_t *)rsp, (uint32_t)is_hw);
        return (uint64_t)(uintptr_t)new_esp;
    }

    return rsp;
}

void init_timer(uint32_t frequency) {
    kprintf("[timer64] initializing at %d Hz\n", frequency);
    timer_freq = frequency;

    /* Configure PIT channel 0: mode 3 (square wave), lobyte/hibyte, binary */
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    /* Unmask IRQ0 (timer) and IRQ1 (keyboard) */
    outb(MASTER_PIC_DATA, 0xFC);

    kprintf("[timer64] divisor=%d freq=%d Hz\n", divisor, frequency);
}

uint32_t get_tick_count(void) { return system_ticks; }

uint32_t get_uptime_seconds(void) {
    if (!timer_freq) return 0;
    return system_ticks / timer_freq;
}
