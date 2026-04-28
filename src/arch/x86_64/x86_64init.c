/*
 * x86_64init.c — 64-bit architecture bring-up
 *
 * Called from arch_impl.c → arch_init() → kernel_main.
 *
 * Order of operations:
 *   1. GDT64 (null, kcode, kdata, ucode, udata, TSS×2)
 *   2. TSS64 (rsp0 for syscall stack)
 *   3. IDT64 (256 × 16-byte descriptors) + PIC remap
 *   4. PMM (physical memory manager — needs multiboot info)
 *   5. 4-level paging (permanent kernel PML4)
 *   6. PIT timer at ~100 Hz
 */
#include "arch/x86_64/x86_64init.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/io.h"
#include "memlayout.h"
#include "lib.h"

/*
 * Temporary kernel stack — top is stored here so tss64_init has a
 * valid rsp0 before the first task switch.  The real per-task kernel
 * stacks are allocated by task_create().
 */
extern char stack_top[];  /* defined in boot.S BSS */

void init_x86_64(void) {
    /* Serial must be first — it is the only console on x86_64. */
    serial_init();
    kprintf("[arch] x86_64 init starting\n");

    /* 1. TSS must exist before GDT loads the TSS descriptor */
    tss64_init((uintptr_t)stack_top);

    /* 2. GDT64: installs null/kcode/kdata/ucode/udata and TSS descriptor,
     *    then does lgdt + ltr. */
    gdt64_init();
    kprintf("[arch] GDT64 loaded\n");

    /* 3. IDT + PIC remap */
    init_idt();
    kprintf("[arch] IDT64 loaded\n");

    /* 4. PIT timer at 100 Hz.
     *    NOTE: Paging is NOT initialised here — init_paging() requires PMM
     *    which is started by kernel_main after arch_init() returns.  The
     *    boot.S temporary page tables remain active until kernel_main calls
     *    arch_paging_init() after pmm_init(). */
    init_timer(100);
    kprintf("[arch] timer 100 Hz\n");

    kprintf("[arch] x86_64 init done\n");
}
