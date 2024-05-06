.section .rodata
my_str:
    .string "hello world\n"

.section .text
.global test_assembly
test_assembly:
    push $my_str
    call printf
    leave
    ret
