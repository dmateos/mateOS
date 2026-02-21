.intel_syntax noprefix

.globl $print

$print:
    push ebp
    mov ebp, esp
    push esi

    mov esi, [ebp + 8]
    test esi, esi
    je .done

    xor edx, edx
.len_loop:
    cmp byte ptr [esi + edx], 0
    je .have_len
    inc edx
    jmp .len_loop

.have_len:
    test edx, edx
    je .done
    mov eax, 1      # SYS_WRITE
    mov ebx, 1      # stdout
    mov ecx, esi
    int 0x80

.done:
    pop esi
    mov esp, ebp
    pop ebp
    ret
