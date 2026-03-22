/*
 * gdt_asm.S — 64-bit GDT flush helper
 *
 * gdt64_flush(gdtr_addr, cs, ds):
 *   Loads GDTR then reloads CS via a far return and DS/ES/SS from ds arg.
 *   This is the standard trick because ljmp in 64-bit mode can't use a
 *   register operand — we push CS:RIP and do retfq instead.
 */
.code64
.section .text
.global gdt64_flush
gdt64_flush:
    /* rdi = gdtr_addr, si = cs selector, dx = ds selector */
    lgdt (%rdi)

    /* Far return to reload CS:
     *   push cs value (zero-extended from si)
     *   push next RIP (.Lflush64)
     *   retfq (pops RIP then CS)
     */
    movzwq %si, %rax          /* rax = cs selector */
    pushq  %rax
    leaq   .Lflush64(%rip), %rax
    pushq  %rax
    lretq

.Lflush64:
    /* Reload data segments */
    movzwq %dx, %rax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs
    ret
