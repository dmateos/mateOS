.section .rodata
my_str:
    .string "hello world\n"

.section .text

.global test_assembly
test_assembly:
    pushl %ebp
    movl %esp, %ebp
    subl $20, %esp

    pushl $my_str
    call printf

    movl %ebp, %esp
    popl %ebp
    ret

.global halt_and_catch_fire
halt_and_catch_fire:
    hlt

.global flush_gdt
flush_gdt:
    movl 4(%esp), %eax  #Load address of gdt into eax
    lgdt (%eax)
    ljmp $0x08, $next
next:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ret


