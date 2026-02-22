/* crt1.o — C runtime startup for TCC-compiled programs on mateOS.
   The kernel places [fake_ret=0][argc][argv] on the user stack. */
.intel_syntax noprefix

.globl _start
.extern main

_start:
    /* argc is at [esp+4], argv at [esp+8] — already on stack for main */
    call main
    /* exit(eax) */
    mov ebx, eax
    mov eax, 2        /* SYS_EXIT */
    int 0x80
1:  jmp 1b
