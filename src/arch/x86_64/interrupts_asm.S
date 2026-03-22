/*
 * interrupts_asm.S — 64-bit interrupt/exception stubs and context switcher
 *
 * Stack layout pushed by CPU on interrupt (ring-3 → ring-0 transition):
 *   RSP+0:  RIP      (user or kernel)
 *   RSP+8:  CS
 *   RSP+16: RFLAGS
 *   RSP+24: RSP      (user, only when ring change)
 *   RSP+32: SS       (user, only when ring change)
 *
 * After our macros push all GPRs the cpu_state64_t layout (see interrupts.h) is:
 *   (low address)
 *   r15, r14, r13, r12, r11, r10, r9, r8
 *   rdi, rsi, rbp, rbx, rdx, rcx, rax   (15 GPRs × 8)
 *   [error_code]                          (inserted by macro; 0 if none)
 *   rip, cs, rflags, rsp, ss             (CPU iret frame × 5)
 *   (high address)
 *
 * Context-switch frame (what schedule() stores in task->stack_top):
 *   Same as above — the timer/yield handler saves everything and calls
 *   timer_handler_switch(rsp, is_hw) which returns the new task's RSP.
 *   We restore from that RSP via the same pop sequence.
 */

.code64
.section .text

/* ── Macro: save all GPRs ────────────────────────────────────────────── */
.macro SAVE_ALL
    push %rax
    push %rcx
    push %rdx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
.endm

/* ── Macro: restore all GPRs ─────────────────────────────────────────── */
.macro RESTORE_ALL
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax
.endm

/* ── Exception without error code ────────────────────────────────────── */
.macro isr_noerrorcode num
  .global isr\num
  .align 16
  isr\num:
    pushq $0                /* dummy error code */
    SAVE_ALL
    mov  %rsp, %rdi         /* arg1: pointer to cpu_state64_t */
    mov  $\num, %rsi        /* arg2: interrupt number */
    call idt_exception_handler
    RESTORE_ALL
    addq $8, %rsp           /* pop error code */
    iretq
.endm

/* ── Exception with error code (CPU already pushed it) ───────────────── */
.macro isr_errorcode num
  .global isr\num
  .align 16
  isr\num:
    /* error code already on stack */
    SAVE_ALL
    mov  %rsp, %rdi         /* arg1: pointer to cpu_state64_t */
    mov  $\num, %rsi        /* arg2: interrupt number */
    call idt_exception_handler
    RESTORE_ALL
    addq $8, %rsp           /* pop error code */
    iretq
.endm

/* ── Hardware IRQ (generic, no context switch) ───────────────────────── */
.macro irq irq_num, isr_num
  .global irq\irq_num
  .align 16
  irq\irq_num:
    pushq $0
    SAVE_ALL
    mov  $\irq_num, %rdi
    mov  $\isr_num, %rsi
    call idt_irq_handler
    RESTORE_ALL
    addq $8, %rsp
    iretq
.endm

/* ── Timer IRQ0 — with context switch ───────────────────────────────── */
.global irq0_task
.align 16
irq0_task:
    pushq $0
    SAVE_ALL
    mov  %rsp, %rdi          /* arg1: saved context RSP */
    mov  $1,   %rsi          /* arg2: is_hw = 1 */
    call timer_handler_switch
    /* rax = new task's RSP (may be same or different task) */
    mov  %rax, %rsp
    RESTORE_ALL
    addq $8, %rsp
    iretq

/* ── Software yield — int 0x20 ───────────────────────────────────────── */
.global yield_task
.align 16
yield_task:
    pushq $0
    SAVE_ALL
    mov  %rsp, %rdi
    mov  $0,   %rsi          /* is_hw = 0 */
    call timer_handler_switch
    mov  %rax, %rsp
    RESTORE_ALL
    addq $8, %rsp
    iretq

/* ── Syscall handler — int 0x80 ──────────────────────────────────────── */
/*
 * 64-bit SysV calling convention:
 *   Syscall args in: rax=num, rdi=arg1, rsi=arg2, rdx=arg3
 *   (Mirrors the 32-bit eax/ebx/ecx/edx convention approximately.)
 *   Return value goes back in rax.
 *
 * We call syscall_handler(num, arg1, arg2, arg3, frame_ptr).
 * frame_ptr is the pointer past the GPRs (to the error_code/iret frame).
 */
.global isr128
.align 16
isr128:
    pushq $0
    SAVE_ALL

    /* syscall_handler(eax, ebx_equiv, ecx_equiv, edx_equiv, frame_ptr)
     * In the 64-bit kernel, syscall args are: rax=num, rdi=arg1, rsi=arg2, rdx=arg3
     * The C handler still has the old (num, arg1, arg2, arg3, frame_ptr) signature.
     * Saved register positions in cpu_state64_t (low→high in stack):
     *   rax at [rsp + 14*8] = rsp + 112
     *   rdi at [rsp + 6*8]  = rsp + 48
     *   rsi at [rsp + 7*8]  = rsp + 56
     *   rdx at [rsp + 4*8]  = rsp + 32
     * (counting from RSP after SAVE_ALL, 15 regs pushed, r15 at RSP)
     * Order: r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax
     *        0   1   2   3   4   5   6  7  8   9   10  11  12  13  14
     */
    mov 112(%rsp), %rdi     /* arg0: syscall number (saved rax) */
    mov  48(%rsp), %rsi     /* arg1: arg1 (saved rdi) */
    mov  56(%rsp), %rdx     /* arg2: arg2 (saved rsi) */
    mov  32(%rsp), %rcx     /* arg3: arg3 (saved rdx) */

    /* frame_ptr: RSP + 15*8 + 8(error) = past all saved regs + error code */
    lea 128(%rsp), %r8      /* arg4: iret frame pointer */

    call syscall_handler

    /* Store return value into saved rax slot so iretq restores it */
    mov %rax, 112(%rsp)

    RESTORE_ALL
    addq $8, %rsp
    iretq

/* ── Exception stubs ─────────────────────────────────────────────────── */
isr_noerrorcode 0
isr_noerrorcode 1
isr_noerrorcode 2
isr_noerrorcode 3
isr_noerrorcode 4
isr_noerrorcode 5
isr_noerrorcode 6
isr_noerrorcode 7
isr_errorcode   8
isr_noerrorcode 9
isr_errorcode   10
isr_errorcode   11
isr_errorcode   12
isr_errorcode   13
isr_errorcode   14
isr_noerrorcode 15
isr_noerrorcode 16
isr_errorcode   17
isr_noerrorcode 18
isr_noerrorcode 19
isr_noerrorcode 20
isr_noerrorcode 21
isr_noerrorcode 22
isr_noerrorcode 23
isr_noerrorcode 24
isr_noerrorcode 25
isr_noerrorcode 26
isr_noerrorcode 27
isr_noerrorcode 28
isr_noerrorcode 29
isr_errorcode   30
isr_noerrorcode 31

/* ── IRQ stubs (generic — no context switch) ─────────────────────────── */
irq 1, 33
irq 2, 34
irq 3, 35
irq 4, 36
irq 5, 37
irq 6, 38
irq 7, 39
irq 8, 40
irq 9, 41
irq 10, 42
irq 11, 43
irq 12, 44
irq 13, 45
irq 14, 46
irq 15, 47
