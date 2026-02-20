; mateOS C compiler runtime crt0 shim
bits 32

section .text
global $_start
extern $main

$_start:
    call    $main
    mov     ebx, eax
    mov     eax, 2
    int     0x80
.hang:
    jmp     .hang
