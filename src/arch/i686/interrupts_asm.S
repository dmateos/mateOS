.section .text

.global flush_idt
flush_idt:
    movl 4(%esp), %eax  #Load address of gdt into eax
    lidt (%eax)
    sti
    ret

.macro isr_errorcode num=0
  .global isr\num
  .align 4
  isr\num:
    # CPU already pushed error code at [ESP]
    # Stack: [error_code] [EIP] [CS] [EFLAGS] (possibly [ESP] [SS] for user mode)
    pusha
    # Now stack: [pusha regs 32 bytes] [error_code] [EIP] [CS] [EFLAGS]
    # Error code is at ESP+32
    mov 32(%esp), %eax          # Get error code from stack
    push %eax                   # Push error code as second arg
    push $\num                  # Push interrupt number as first arg
    call idt_exception_handler
    add $8, %esp                # Cleanup both pushed args
    popa
    add $4, %esp                # Cleanup CPU-pushed error code
    iret
.endm

.macro isr_noerrorcode num=0
  .global isr\num
  .align 4
  isr\num:
    pusha
    push $0
    push $\num
    call idt_exception_handler
    add $8, %esp                #cleanup bytes pushed by push above
    popa
    iret
.endm

.macro irq irq_num=0, isr_num=0
  .global irq\irq_num
  .align 4
  irq\irq_num:
    pusha
    push $\irq_num
    push $\isr_num
    call idt_irq_handler
    add $8, %esp                #cleanup bytes pushed by push above
    popa
    iret
.endm

# Special handler for IRQ0 (timer) that supports context switching
# Called from real hardware IRQ0 - sends EOI
.global irq0_task
.align 4
irq0_task:
    pusha                       # Save all general registers

    # Save segment registers
    push %ds
    push %es
    push %fs
    push %gs

    # Load kernel data segment into segment registers
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    # Call timer_handler_switch(esp, is_hw=1)
    mov %esp, %eax              # Save context ESP before pushing args
    push $1                     # is_hw = 1 (real hardware IRQ)
    push %eax                   # esp = saved context pointer
    call timer_handler_switch
    add $8, %esp                # cleanup both args
    mov %eax, %esp              # Switch to new stack if needed

    # Restore segment registers (may be user or kernel)
    pop %gs
    pop %fs
    pop %es
    pop %ds

    popa                        # Restore registers (possibly from new task)
    iret                        # Return from interrupt

# Software yield handler - called via int $0x20 from task_yield()
# Does NOT send EOI (no real hardware IRQ happened)
.global yield_task
.align 4
yield_task:
    pusha                       # Save all general registers

    # Save segment registers
    push %ds
    push %es
    push %fs
    push %gs

    # Load kernel data segment into segment registers
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    # Call timer_handler_switch(esp, is_hw=0)
    mov %esp, %eax              # Save context ESP before pushing args
    push $0                     # is_hw = 0 (software interrupt)
    push %eax                   # esp = saved context pointer
    call timer_handler_switch
    add $8, %esp                # cleanup both args
    mov %eax, %esp              # Switch to new stack if needed

    # Restore segment registers (may be user or kernel)
    pop %gs
    pop %fs
    pop %es
    pop %ds

    popa                        # Restore registers (possibly from new task)
    iret                        # Return from interrupt

isr_noerrorcode 0
isr_noerrorcode 1
isr_noerrorcode 2
isr_noerrorcode 3
isr_noerrorcode 4
isr_noerrorcode 5
isr_noerrorcode 6
isr_noerrorcode 7
isr_errorcode 8
isr_noerrorcode 9
isr_errorcode 10
isr_errorcode 11
isr_errorcode 12
isr_errorcode 13
isr_errorcode 14
isr_noerrorcode 15
isr_noerrorcode 16
isr_errorcode 17
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
isr_errorcode 30
isr_noerrorcode 31

irq 0,32
irq 1,33
irq 2,34
irq 3,35
irq 4,36
irq 5,37
irq 6,38
irq 7,39
irq 8,40
irq 9,41
irq 10,42
irq 11,43
irq 12,44
irq 13,45
irq 14,46
irq 15,47

# Syscall handler (int 0x80)
# User mode can call this because IDT entry has DPL=3
# Arguments in registers: eax=syscall#, ebx=arg1, ecx=arg2, edx=arg3
# Return value in eax
#
# Stack layout after pusha (ring 3 caller):
#   ESP+0:  EDI  \
#   ESP+4:  ESI   |
#   ESP+8:  EBP   |
#   ESP+12: ESP   | pusha
#   ESP+16: EBX   |
#   ESP+20: EDX   |
#   ESP+24: ECX   |
#   ESP+28: EAX  /
#   ESP+32: EIP  \
#   ESP+36: CS    |
#   ESP+40: EFLAGS| iret frame (ring transition)
#   ESP+44: ESP   |
#   ESP+48: SS   /
.global isr128
.align 4
isr128:
    # Save all registers
    pusha

    # Push syscall arguments for C handler
    # syscall_handler(eax, ebx, ecx, edx, frame_ptr)
    mov %esp, %esi
    add $32, %esi        # frame_ptr: points past pusha to iret frame
    push %esi            # arg5: frame pointer
    push %edx            # arg4: edx
    push %ecx            # arg3: ecx
    push %ebx            # arg2: ebx
    push %eax            # arg1: syscall number

    call syscall_handler

    # Clean up pushed arguments
    add $20, %esp

    # Store return value where popa will restore it to eax
    mov %eax, 28(%esp)

    popa
    iret
