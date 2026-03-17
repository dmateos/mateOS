/*
 * setjmp / longjmp for mateOS userland (i386 cdecl)
 *
 * jmp_buf layout (6 ints = 24 bytes):
 *   [0]  ebx
 *   [1]  esi
 *   [2]  edi
 *   [3]  ebp
 *   [4]  esp  (caller's esp, pointing at the return-address slot on entry)
 *   [5]  eip  (return address = instruction after the call to _setjmp)
 *
 * Because these are separate translation units the compiler generates correct
 * cdecl call sites: push args, call, add $N (cleanup).  We do NOT need to
 * know N — longjmp restores esp to the saved value (= esp at call entry,
 * pointing at the return addr slot) and jumps to eip; the caller's "add $N"
 * cleanup then runs normally and unwinds the call frame.
 */

    .text
    .globl _setjmp
    .type  _setjmp, @function
_setjmp:
    /* On entry: esp+0 = ret addr, esp+4 = env (jmp_buf *) */
    movl  4(%esp), %ecx         /* ecx = env */
    movl  %ebx,  0(%ecx)
    movl  %esi,  4(%ecx)
    movl  %edi,  8(%ecx)
    movl  %ebp, 12(%ecx)
    leal  4(%esp), %edx         /* edx = esp+4 (value after ret pops ret addr) */
    movl  %edx, 16(%ecx)
    movl  (%esp), %edx          /* edx = return address */
    movl  %edx, 20(%ecx)
    xorl  %eax, %eax            /* return 0 */
    ret
    .size _setjmp, .-_setjmp

    /* setjmp is an alias for _setjmp */
    .globl setjmp
    .type  setjmp, @function
setjmp:
    jmp   _setjmp
    .size setjmp, .-setjmp

    .globl longjmp
    .type  longjmp, @function
longjmp:
    /* On entry: esp+0 = ret addr, esp+4 = env, esp+8 = val */
    movl  4(%esp), %ecx         /* ecx = env */
    movl  8(%esp), %eax         /* eax = val */
    testl %eax, %eax
    jnz   1f
    movl  $1, %eax              /* val==0 must return 1 (C standard) */
1:
    movl   0(%ecx), %ebx
    movl   4(%ecx), %esi
    movl   8(%ecx), %edi
    movl  12(%ecx), %ebp
    movl  16(%ecx), %esp        /* restore esp to saved value */
    jmpl  *20(%ecx)             /* jump to saved eip (= instr after call) */
    .size longjmp, .-longjmp

    /* sigsetjmp / siglongjmp — ignore savesigs, delegate to setjmp/longjmp */
    .globl sigsetjmp
    .type  sigsetjmp, @function
sigsetjmp:
    /* args: env (4(%esp)), savesigs (8(%esp)) — drop savesigs, tail-call _setjmp */
    movl  4(%esp), %ecx
    movl  %ecx, 4(%esp)         /* env already in place */
    jmp   _setjmp
    .size sigsetjmp, .-sigsetjmp

    .globl siglongjmp
    .type  siglongjmp, @function
siglongjmp:
    jmp   longjmp
    .size siglongjmp, .-siglongjmp
