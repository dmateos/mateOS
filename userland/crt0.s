.intel_syntax noprefix

.globl $_start
.extern $main

$_start:
    call $main
    mov ebx, eax
    mov eax, 2
    int 0x80
1:
    jmp 1b
