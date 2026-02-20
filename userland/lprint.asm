; mateOS C compiler temporary print helper
bits 32

section .text
global $print

$print:
    push    ebp
    mov     ebp, esp
    push    ebx
    push    ecx
    push    esi
    push    edx
    mov     ecx, [ebp+8]
    mov     esi, ecx
    xor     edx, edx
.strlen_loop:
    cmp     byte [esi], 0
    je      .strlen_done
    inc     esi
    inc     edx
    jmp     .strlen_loop
.strlen_done:
    mov     eax, 1
    mov     ebx, 1
    int     0x80
    xor     eax, eax
    pop     edx
    pop     esi
    pop     ecx
    pop     ebx
    leave
    ret
