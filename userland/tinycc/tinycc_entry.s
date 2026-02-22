.intel_syntax noprefix

.globl _start
.extern main

_start:
    # Debug: print entry marker
    push 11
    push offset entry_msg
    push 1
    mov eax, 1
    int 0x80
    add esp, 12

    mov edx, [esp + 4]   # argc
    mov eax, [esp + 8]   # argv
    push eax
    push edx
    call main
    mov ebx, eax
    mov eax, 2
    int 0x80
1:
    jmp 1b

.section .rodata
entry_msg:
    .ascii "TCC: entry\n"
