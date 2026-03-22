/*
 * arch_impl.c — i686 implementation of arch_interface.h
 *
 * Every function here is a thin wrapper (or trivial adapter) over the
 * existing i686 code.  No new logic lives here — the real work is still
 * in paging.c, tss.c, gdt.c, interrupts.c, etc.
 *
 * Concrete definitions for the two opaque types:
 *
 *   arch_aspace_t   ≡ page_directory_t   (1024-entry PD)
 *   arch_irq_frame_t ≡ iret_frame_t      (eip/cs/eflags/esp/ss on stack)
 */

#include "arch/arch_interface.h"

/* Pull in all existing i686 headers — implementation details stay here. */
#include "arch/i686/cpu.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/paging.h"
#include "arch/i686/tss.h"
#include "arch/i686/686init.h"
#include "lib.h"
#include "memlayout.h"

/* ------------------------------------------------------------------ */
/* Concrete type definitions                                           */
/* ------------------------------------------------------------------ */

/*
 * On i686 the address space IS the page directory.
 * We typedef it as arch_aspace_t via the struct tag.
 */
struct arch_aspace {
    /* Embed the real PD so that cast is a no-op. */
    page_directory_t pd;
};

/*
 * On i686 the interrupt return frame IS the iret_frame_t.
 */
struct arch_irq_frame {
    iret_frame_t f;
};

/* ------------------------------------------------------------------ */
/* Address space                                                       */
/* ------------------------------------------------------------------ */

arch_aspace_t *arch_aspace_create(void) {
    /* paging_create_address_space() returns page_directory_t *,
     * which is layout-compatible with arch_aspace_t *. */
    return (arch_aspace_t *)paging_create_address_space();
}

void arch_aspace_destroy(arch_aspace_t *as) {
    paging_destroy_address_space((page_directory_t *)as);
}

void arch_aspace_switch(arch_aspace_t *as) {
    paging_switch((page_directory_t *)as);
}

arch_aspace_t *arch_aspace_kernel(void) {
    return (arch_aspace_t *)paging_get_kernel_dir();
}

int arch_aspace_map(arch_aspace_t *as, uintptr_t vaddr, uintptr_t paddr,
                    uint32_t flags) {
    /* Translate arch-neutral flags to i686 PAGE_* flags.
     * The values happen to be identical, but going through this
     * translation keeps the boundary explicit. */
    uint32_t hw_flags = 0;
    if (flags & ARCH_PAGE_PRESENT) hw_flags |= PAGE_PRESENT;
    if (flags & ARCH_PAGE_WRITE)   hw_flags |= PAGE_WRITE;
    if (flags & ARCH_PAGE_USER)    hw_flags |= PAGE_USER;
    return paging_map_page((page_directory_t *)as,
                           (uint32_t)vaddr, (uint32_t)paddr, hw_flags);
}

void arch_aspace_unmap(arch_aspace_t *as, uintptr_t vaddr) {
    paging_unmap_page((page_directory_t *)as, (uint32_t)vaddr);
}

void arch_aspace_map_mmio(uintptr_t phys, uint32_t size) {
    paging_map_vbe((uint32_t)phys, size);
}

uintptr_t arch_aspace_user_phys(arch_aspace_t *as, uintptr_t vaddr) {
    page_directory_t *pd = (page_directory_t *)as;
    uint32_t dir_idx   = (uint32_t)vaddr >> 22;
    uint32_t table_idx = ((uint32_t)vaddr >> 12) & 0x3FFu;

    if (!(pd->tables[dir_idx] & PAGE_PRESENT))
        return 0;

    page_table_t *pt = (page_table_t *)PHYS_TO_KVIRT(
        pd->tables[dir_idx] & ~0xFFFu);
    uint32_t pte = pt->pages[table_idx];

    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER))
        return 0;

    return (uintptr_t)(pte & ~0xFFFu);
}

/* ------------------------------------------------------------------ */
/* Task stack initialisation                                           */
/* ------------------------------------------------------------------ */

/*
 * Build the initial kernel-task stack frame that schedule() will pop on
 * the first context switch to this task.  Mirrors the layout pushed by
 * the irq0_task / yield_task stubs in interrupts_asm.S:
 *
 *   [DS][ES][FS][GS]  ← segment registers (popped first)
 *   [EDI..EAX]        ← pusha-equivalent  (8 dwords)
 *   [EIP][CS][EFLAGS] ← iret frame        (kernel privilege, no ESP/SS)
 */
uint32_t *arch_task_init_kernel(void (*entry)(void), uint32_t *stack_top) {
    uint32_t *sp = stack_top;

    /* iret frame (kernel mode — no ring transition, so no ESP/SS) */
    *(--sp) = ARCH_EFLAGS_DEFAULT;   /* EFLAGS */
    *(--sp) = KERNEL_CODE_SEG;       /* CS     */
    *(--sp) = (uint32_t)entry;       /* EIP    */

    /* pusha-equivalent (all zero for a fresh task) */
    *(--sp) = 0; /* EAX */
    *(--sp) = 0; /* ECX */
    *(--sp) = 0; /* EDX */
    *(--sp) = 0; /* EBX */
    *(--sp) = 0; /* ESP (ignored by popa) */
    *(--sp) = 0; /* EBP */
    *(--sp) = 0; /* ESI */
    *(--sp) = 0; /* EDI */

    /* Segment registers */
    *(--sp) = KERNEL_DATA_SEG; /* GS */
    *(--sp) = KERNEL_DATA_SEG; /* FS */
    *(--sp) = KERNEL_DATA_SEG; /* ES */
    *(--sp) = KERNEL_DATA_SEG; /* DS */

    return sp;
}

/*
 * Build the initial kernel stack frame for a user-mode task.
 * The full user-mode iret frame (with ESP and SS for ring-3 → ring-0
 * transition) goes on top, followed by the pusha-equivalent zeros and
 * segment registers, exactly as the irq0_task handler expects.
 */
uint32_t *arch_task_init_user(uintptr_t elf_entry, uintptr_t user_esp,
                              uint32_t *kstack_top) {
    uint32_t *sp = kstack_top;

    /* User-mode iret frame (ring transition includes ESP + SS) */
    *(--sp) = USER_DATA_SEL;         /* SS     */
    *(--sp) = (uint32_t)user_esp;    /* ESP    */
    *(--sp) = ARCH_EFLAGS_DEFAULT;   /* EFLAGS */
    *(--sp) = USER_CODE_SEL;         /* CS     */
    *(--sp) = (uint32_t)elf_entry;   /* EIP    */

    /* pusha-equivalent zeros */
    *(--sp) = 0; /* EAX */
    *(--sp) = 0; /* ECX */
    *(--sp) = 0; /* EDX */
    *(--sp) = 0; /* EBX */
    *(--sp) = 0; /* ESP (ignored) */
    *(--sp) = 0; /* EBP */
    *(--sp) = 0; /* ESI */
    *(--sp) = 0; /* EDI */

    /* Segment registers — user data segment */
    *(--sp) = USER_DATA_SEL; /* GS */
    *(--sp) = USER_DATA_SEL; /* FS */
    *(--sp) = USER_DATA_SEL; /* ES */
    *(--sp) = USER_DATA_SEL; /* DS */

    return sp;
}

void arch_task_set_kernel_stack(uintptr_t kstack_top) {
    tss_set_kernel_stack((uint32_t)kstack_top);
}

/* ------------------------------------------------------------------ */
/* Exec: redirect iret frame                                           */
/* ------------------------------------------------------------------ */

void arch_set_return_context(arch_irq_frame_t *frame, uintptr_t elf_entry,
                             uintptr_t user_esp) {
    /* frame->f is the iret_frame_t sitting on the kernel stack. */
    frame->f.eip    = (uint32_t)elf_entry;
    frame->f.cs     = USER_CODE_SEL;
    frame->f.eflags = ARCH_EFLAGS_DEFAULT;
    frame->f.esp    = (uint32_t)user_esp;
    frame->f.ss     = USER_DATA_SEL;
}

/* ------------------------------------------------------------------ */
/* Syscall glue                                                        */
/* ------------------------------------------------------------------ */

/*
 * The i686 syscall stub (isr128 in interrupts_asm.S) pushes the
 * register set on the kernel stack and then calls:
 *
 *   syscall_handler(eax, ebx, ecx, edx, frame_ptr)
 *
 * We keep that existing ABI for now: state is actually the raw uint32_t
 * passed as the first argument — there is no cpu_state_t * here.
 *
 * The assembly wrapper passes args already unpacked, so
 * arch_syscall_args_from_state / arch_syscall_set_retval are no-ops
 * on i686 (the dispatcher still receives them the old way and calls
 * these only to remain interface-compatible when porting).
 *
 * NOTE: on x86_64 the assembly will call a different entry point that
 * actually uses these functions to unpack the register file.
 */
void arch_syscall_args_from_state(void *state, arch_syscall_args_t *out) {
    /* On i686 the dispatcher receives args directly — this path is
     * only exercised by future arches.  Provide a safe fallback. */
    (void)state;
    (void)out;
}

void arch_syscall_set_retval(void *state, uintptr_t retval) {
    (void)state;
    (void)retval;
}

arch_irq_frame_t *arch_irq_frame_from_state(void *state) {
    /*
     * isr128 passes the iret_frame_t * directly as the 'frame' argument
     * to syscall_handler().  state here is that raw pointer.
     * arch_irq_frame_t and iret_frame_t are layout-compatible.
     */
    return (arch_irq_frame_t *)state;
}

/* ------------------------------------------------------------------ */
/* Architecture initialisation                                         */
/* ------------------------------------------------------------------ */

void arch_init(void) {
    init_686();
}
