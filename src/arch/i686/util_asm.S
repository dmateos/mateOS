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
