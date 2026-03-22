#include "lib.h"

#if defined(__linux__)
#error                                                                         \
    "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

#if !defined(__i386__) && !defined(__x86_64__)
#error "i386 or x86_64 required"
#endif
