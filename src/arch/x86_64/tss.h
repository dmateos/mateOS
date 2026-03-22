/*
 * tss.h — 64-bit Task State Segment
 *
 * TSS64 is used only for rsp0 (kernel stack on ring-3→ring-0 transition)
 * and IST pointers (for NMI, double-fault, etc.).  Segment fields from
 * the 32-bit TSS are not used in 64-bit mode.
 *
 * Size: 104 bytes (mandated by the Intel manual).
 */
#ifndef _ARCH_X86_64_TSS_H
#define _ARCH_X86_64_TSS_H

#include "lib.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel stack pointer (ring 0) */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        /* IST1..IST7 — interrupt stack table */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;    /* byte offset to I/O permission bitmap */
} __attribute__((packed)) tss64_t;

void     tss64_init(uintptr_t kernel_stack);
void     tss64_set_rsp0(uintptr_t rsp0);
tss64_t *tss64_get(void);

#endif /* _ARCH_X86_64_TSS_H */
