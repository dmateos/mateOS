#ifndef MATEOS_ASSERT_H
#define MATEOS_ASSERT_H

void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func);

#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, (unsigned int)__LINE__, __func__))

#endif
