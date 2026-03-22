/*
 * arch_impl.c — x86_64 implementation of arch_interface.h  [STUB]
 *
 * All functions here are stubs returning safe error values.
 * Fill them in as the x86_64 port progresses:
 *   Phase 1: arch_aspace_* (4-level paging)
 *   Phase 2: arch_task_init_* (64-bit frame layout)
 *   Phase 3: arch_set_return_context, arch_syscall_* (SYSCALL/SYSRET or int 0x80)
 *   Phase 4: arch_init (GDT64, IDT64, TSS64, PIC, timer)
 *
 * Concrete type definitions:
 *   arch_aspace_t    — will wrap pml4_t
 *   arch_irq_frame_t — will wrap the 64-bit iret frame pushed by stubs
 */

#include "arch/arch_interface.h"
#include "arch/x86_64/x86_64init.h"
#include "arch/x86_64/paging.h"
#include "lib.h"

/* ------------------------------------------------------------------ */
/* Concrete opaque types                                               */
/* ------------------------------------------------------------------ */

struct arch_aspace {
    pml4_t pml4; /* root of the 4-level page table */
};

struct arch_irq_frame {
    /* 64-bit iret frame: rip, cs, rflags, rsp, ss */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* ------------------------------------------------------------------ */
/* Address space — STUB                                                */
/* ------------------------------------------------------------------ */

arch_aspace_t *arch_aspace_create(void) {
    /* TODO: allocate PML4 page, copy kernel mappings */
    return NULL;
}

void arch_aspace_destroy(arch_aspace_t *as) {
    /* TODO: walk and free user-space tables */
    (void)as;
}

void arch_aspace_switch(arch_aspace_t *as) {
    /* TODO: load CR3 with physical address of PML4 */
    (void)as;
}

arch_aspace_t *arch_aspace_kernel(void) {
    /* TODO: return pointer to boot-time kernel PML4 */
    return NULL;
}

int arch_aspace_map(arch_aspace_t *as, uintptr_t vaddr, uintptr_t paddr,
                    uint32_t flags) {
    /* TODO: walk/allocate PML4→PDPT→PD→PT, set entry */
    (void)as; (void)vaddr; (void)paddr; (void)flags;
    return -1;
}

void arch_aspace_unmap(arch_aspace_t *as, uintptr_t vaddr) {
    /* TODO: clear PT entry, invlpg */
    (void)as; (void)vaddr;
}

void arch_aspace_map_mmio(uintptr_t phys, uint32_t size) {
    /* TODO: map MMIO into kernel PML4 */
    (void)phys; (void)size;
}

uintptr_t arch_aspace_user_phys(arch_aspace_t *as, uintptr_t vaddr) {
    /* TODO: walk PML4→PT, return phys if user-mapped */
    (void)as; (void)vaddr;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Task stack initialisation — STUB                                    */
/* ------------------------------------------------------------------ */

uint32_t *arch_task_init_kernel(void (*entry)(void), uint32_t *stack_top) {
    /*
     * TODO: push 64-bit kernel iret frame:
     *   rflags, cs (kernel), rip (entry)
     * then push saved GPRs (r15..rax).
     * Return new RSP.
     */
    (void)entry; (void)stack_top;
    return NULL;
}

uint32_t *arch_task_init_user(uintptr_t elf_entry, uintptr_t user_esp,
                              uint32_t *kstack_top) {
    /*
     * TODO: push 64-bit user iret frame:
     *   ss (user), rsp (user_esp), rflags, cs (user), rip (elf_entry)
     * then push saved GPRs.
     * Return new kernel RSP.
     */
    (void)elf_entry; (void)user_esp; (void)kstack_top;
    return NULL;
}

void arch_task_set_kernel_stack(uintptr_t kstack_top) {
    /* TODO: update TSS64.rsp0 */
    (void)kstack_top;
}

/* ------------------------------------------------------------------ */
/* Exec redirect — STUB                                                */
/* ------------------------------------------------------------------ */

void arch_set_return_context(arch_irq_frame_t *frame, uintptr_t elf_entry,
                             uintptr_t user_esp) {
    /* TODO: update frame->rip, frame->cs, frame->rflags, frame->rsp */
    (void)frame; (void)elf_entry; (void)user_esp;
}

/* ------------------------------------------------------------------ */
/* Syscall glue — STUB                                                 */
/* ------------------------------------------------------------------ */

void arch_syscall_args_from_state(void *state, arch_syscall_args_t *out) {
    /*
     * TODO: cast state to cpu_state_t *, extract:
     *   out->num  = state->rax
     *   out->arg1 = state->rdi
     *   out->arg2 = state->rsi
     *   out->arg3 = state->rdx
     */
    (void)state;
    if (out) { out->num = out->arg1 = out->arg2 = out->arg3 = 0; }
}

void arch_syscall_set_retval(void *state, uintptr_t retval) {
    /* TODO: ((cpu_state_t *)state)->rax = retval; */
    (void)state; (void)retval;
}

arch_irq_frame_t *arch_irq_frame_from_state(void *state) {
    /* TODO: return pointer to the iret frame embedded in cpu_state_t */
    (void)state;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Architecture initialisation — STUB                                  */
/* ------------------------------------------------------------------ */

void arch_init(void) {
    init_x86_64();
}
