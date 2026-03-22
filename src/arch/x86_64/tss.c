/*
 * tss.c — 64-bit TSS implementation
 *
 * We keep a single static TSS.  The GDT descriptor is installed by gdt.c
 * (which calls tss64_get() to get the TSS address and size).
 * tss64_set_rsp0() is called each time we switch to a new task so that
 * exceptions taken in ring 3 switch to the correct kernel stack.
 */
#include "arch/x86_64/tss.h"
#include "lib.h"

static tss64_t tss64 __attribute__((aligned(4096)));

void tss64_init(uintptr_t kernel_stack) {
    memset(&tss64, 0, sizeof(tss64));
    tss64.rsp0      = kernel_stack;
    tss64.iomap_base = sizeof(tss64_t);   /* disable I/O bitmap */
    kprintf("[tss64] initialized rsp0=0x%x\n", (uint32_t)kernel_stack);
}

void tss64_set_rsp0(uintptr_t rsp0) {
    tss64.rsp0 = rsp0;
}

tss64_t *tss64_get(void) {
    return &tss64;
}
