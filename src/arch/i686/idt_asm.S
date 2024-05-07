.section .text

.macro isr_errorcode num=0
  .global isr\num
  isr\num:
    cli
    push $\num
    jmp isr_common_stub
.endm

.macro isr_noerrorcode num=0
  .global isr\num
  isr\num:
    cli
    push $0
    jmp isr_common_stub
.endm

.global isr_common_stub
isr_common_stub:
    call idt_exception_handler
    add $12, %esp                #cleanup bytes pushed by push above
    iret

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
isr_noerrorcode 17
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
isr_noerrorcode 30
isr_noerrorcode 31
