#ifndef _SMALLERC_STDLIB_H
#define _SMALLERC_STDLIB_H

#include <stddef.h>

#define EXIT_FAILURE 1

void exit(int code);
int atoi(const char *s);
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);

#endif
