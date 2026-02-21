#ifndef MATEOS_SETJMP_H
#define MATEOS_SETJMP_H

typedef int jmp_buf[6];
typedef int sigjmp_buf[6];

int _setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
int sigsetjmp(sigjmp_buf env, int savesigs);
void siglongjmp(sigjmp_buf env, int val);

#endif
