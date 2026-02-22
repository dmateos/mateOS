#ifndef MATEOS_SYS_UCONTEXT_H
#define MATEOS_SYS_UCONTEXT_H

typedef struct {
    unsigned int gregs[32];
} mateos_mcontext_t;

typedef struct {
    mateos_mcontext_t uc_mcontext;
} ucontext_t;

#ifndef REG_EIP
#define REG_EIP 14
#endif
#ifndef REG_EBP
#define REG_EBP 6
#endif

#endif
