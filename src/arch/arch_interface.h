/*
 * arch_interface.h — Architecture abstraction contract
 *
 * Every architecture provides implementations of every function declared here.
 * Generic kernel code (task.c, syscall.c, …) includes only this header and
 * "arch/arch.h" — never any arch/i686/* or arch/x86_64/* header directly.
 *
 * Opaque handle types keep page-table and interrupt-frame internals hidden from
 * the generic kernel:
 *
 *   arch_aspace_t    - per-process address space (wraps page_directory_t on
 *                     i686, PML4 on x86_64, page-table on ARM, etc.)
 *   arch_irq_frame_t - interrupt/exception return frame on the kernel stack
 *                     (wraps iret_frame_t on i686, etc.)
 *
 * Caller-side page-flag constants are defined here in arch-neutral terms:
 *   ARCH_PAGE_PRESENT, ARCH_PAGE_WRITE, ARCH_PAGE_USER
 * The arch implementation maps these to whatever the hardware uses.
 */

#ifndef _ARCH_INTERFACE_H
#define _ARCH_INTERFACE_H

#include "lib.h"

/* ------------------------------------------------------------------ */
/* Arch-neutral page flags (passed to arch_aspace_map)                */
/* ------------------------------------------------------------------ */
#define ARCH_PAGE_PRESENT 0x1
#define ARCH_PAGE_WRITE   0x2
#define ARCH_PAGE_USER    0x4

/* ------------------------------------------------------------------ */
/* Opaque types — defined concretely by each arch in arch_impl.c      */
/* ------------------------------------------------------------------ */

/* Per-process address space handle. */
typedef struct arch_aspace arch_aspace_t;

/* Interrupt/exception return frame on the kernel stack.
 * Passed to arch_set_return_context() and arch_syscall_args_from_frame(). */
typedef struct arch_irq_frame arch_irq_frame_t;

/* ------------------------------------------------------------------ */
/* Syscall argument bundle — hides register naming from dispatcher     */
/* ------------------------------------------------------------------ */
typedef struct {
    uintptr_t num;  /* syscall number (eax / rax) */
    uintptr_t arg1; /* first  arg    (ebx / rdi)  */
    uintptr_t arg2; /* second arg    (ecx / rsi)  */
    uintptr_t arg3; /* third  arg    (edx / rdx)  */
} arch_syscall_args_t;

/* ------------------------------------------------------------------ */
/* Address space operations                                            */
/* ------------------------------------------------------------------ */

/* Allocate a new user address space with kernel mappings pre-shared.
 * Returns NULL on OOM. */
arch_aspace_t *arch_aspace_create(void);

/* Free all user-space page tables and the root structure.
 * Kernel mappings are shared and must NOT be freed. */
void arch_aspace_destroy(arch_aspace_t *as);

/* Load this address space into CR3 (or equivalent). */
void arch_aspace_switch(arch_aspace_t *as);

/* Return the single shared kernel address space. */
arch_aspace_t *arch_aspace_kernel(void);

/* Map a single 4 KB page: vaddr→paddr with arch-neutral flags.
 * Returns 0 on success, -1 on failure. */
int arch_aspace_map(arch_aspace_t *as, uintptr_t vaddr, uintptr_t paddr,
                    uint32_t flags);

/* Unmap a single 4 KB page. */
void arch_aspace_unmap(arch_aspace_t *as, uintptr_t vaddr);

/* Translate a virtual address to its mapped physical address.
 * Returns the physical frame address (page-aligned) if the page is present
 * AND is a user-accessible page (ARCH_PAGE_USER set).
 * Returns 0 if the page is not mapped or is kernel-only. */
uintptr_t arch_aspace_user_phys(arch_aspace_t *as, uintptr_t vaddr);

/* Map a physically-contiguous region into the kernel address space
 * (used for MMIO / VBE framebuffer). */
void arch_aspace_map_mmio(uintptr_t phys, uint32_t size);

/* ------------------------------------------------------------------ */
/* Kernel-stack task initialisation                                    */
/* ------------------------------------------------------------------ */

/* Initialise a kernel-mode task's stack so that the first context
 * switch will transfer control to entry().
 * stack_top points one word past the top of the allocated stack buffer.
 * Returns the new stack pointer (store in task->stack_top). */
uint32_t *arch_task_init_kernel(void (*entry)(void), uint32_t *stack_top);

/* Initialise a user-mode task's kernel stack for its first iret to
 * user space.  elf_entry and user_esp are the user-space values.
 * kstack_top points one word past the top of the kernel-stack buffer.
 * Returns the new kernel stack pointer (store in task->stack_top). */
uint32_t *arch_task_init_user(uintptr_t elf_entry, uintptr_t user_esp,
                              uint32_t *kstack_top);

/* Update the hardware TSS / MSR so that exceptions taken in user mode
 * switch to this kernel-stack pointer. */
void arch_task_set_kernel_stack(uintptr_t kstack_top);

/* ------------------------------------------------------------------ */
/* Exec: redirect an existing task's iret frame                        */
/* ------------------------------------------------------------------ */

/* Modify the interrupt-return frame so that when the syscall handler
 * returns, the task resumes at elf_entry with user_esp as its stack.
 * frame is the arch_irq_frame_t * passed into syscall_handler(). */
void arch_set_return_context(arch_irq_frame_t *frame, uintptr_t elf_entry,
                             uintptr_t user_esp);

/* ------------------------------------------------------------------ */
/* Syscall glue                                                        */
/* ------------------------------------------------------------------ */

/* Extract the syscall argument bundle from the cpu state pointer
 * passed through from the interrupt stub.  state is the raw void*
 * that the assembly pushes before calling syscall_handler_impl(). */
void arch_syscall_args_from_state(void *state, arch_syscall_args_t *out);

/* Write the syscall return value back into the cpu state so that the
 * calling user task sees it in the return register after iret. */
void arch_syscall_set_retval(void *state, uintptr_t retval);

/* Cast the raw frame pointer to an arch_irq_frame_t for sys_exec. */
arch_irq_frame_t *arch_irq_frame_from_state(void *state);

/* ------------------------------------------------------------------ */
/* Architecture initialisation                                         */
/* ------------------------------------------------------------------ */

/* One-shot arch initialisation: GDT, IDT, paging, TSS, timer.
 * Replaces init_686() / init_x86_64(). */
void arch_init(void);

#endif /* _ARCH_INTERFACE_H */
