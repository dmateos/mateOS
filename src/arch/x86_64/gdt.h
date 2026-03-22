/*
 * gdt.h — 64-bit Global Descriptor Table
 *
 * In long mode segment limits/bases are mostly ignored (except for FS/GS used
 * by the kernel for per-CPU data, and the TSS descriptor).  We keep a minimal
 * 7-entry GDT matching the 32-bit layout plus a 64-bit TSS (which takes two
 * consecutive 8-byte slots).
 *
 * Indices (× 8 = byte offset = selector with RPL=0):
 *   0  null
 *   1  kernel code  (DPL=0, L=1)    → 0x08
 *   2  kernel data  (DPL=0)         → 0x10
 *   3  user code    (DPL=3, L=1)    → 0x18 | 3 = 0x1B
 *   4  user data    (DPL=3)         → 0x20 | 3 = 0x23
 *   5  TSS low      (64-bit TSS)    → 0x28
 *   6  TSS high     (upper 32-bits of base, always 0 for us since kernel < 4GB)
 */
#ifndef _ARCH_X86_64_GDT_H
#define _ARCH_X86_64_GDT_H

#include "lib.h"

/* Segment selectors */
#define GDT64_KERNEL_CODE  0x08
#define GDT64_KERNEL_DATA  0x10
#define GDT64_USER_CODE    0x1B   /* 0x18 | RPL 3 */
#define GDT64_USER_DATA    0x23   /* 0x20 | RPL 3 */
#define GDT64_TSS_SEL      0x28

void gdt64_init(void);

#endif /* _ARCH_X86_64_GDT_H */
