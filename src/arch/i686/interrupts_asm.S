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
    pusha
    push $\num
    call idt_exception_handler
    add $8, %esp                #cleanup bytes pushed by push above
    popa
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
    push $\isr_num
    push $\irq_num
    call idt_irq_handler
    add $8, %esp                #cleanup bytes pushed by push above
    popa
    iret
.endm

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
