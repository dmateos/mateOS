/*
 * x86_64init.c — 64-bit architecture bring-up  [STUB]
 *
 * Order of operations (to be filled in):
 *   1. Set up GDT64 (null, kcode, kdata, ucode, udata, TSS×2)
 *   2. Load GDTR (lgdt)
 *   3. Reload segment registers (cs via far return, ds/es/ss/fs/gs = 0)
 *   4. Set up TSS64 (rsp0 for syscall stack, IST stacks for NMI/DF/MC)
 *   5. Load TR (ltr)
 *   6. Set up IDT64 (256 × 16-byte descriptors)
 *   7. Load IDTR (lidt)
 *   8. Remap PIC (or configure APIC)
 *   9. Set up 4-level page tables (already done in boot.S on real hw)
 *  10. Configure PIT / APIC timer at 100 Hz
 *  11. Enable SSE (if desired)
 *  12. Set EFER.SCE for SYSCALL/SYSRET (optional — int 0x80 also works)
 */

#include "arch/x86_64/x86_64init.h"
#include "lib.h"

void init_x86_64(void) {
    /* TODO: implement 64-bit bring-up sequence */
    kprintf("[arch] x86_64 init — NOT YET IMPLEMENTED\n");
    while (1) {} /* halt — don't let kernel continue with no arch */
}
