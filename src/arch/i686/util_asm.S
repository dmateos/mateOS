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
    jmp $0x08, $next
next:      
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret
