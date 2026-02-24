#ifndef _DOOM_STDLIB_H
#define _DOOM_STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);

int abs(int x);
int atoi(const char *s);
double atof(const char *s);
char *getenv(const char *name);
int system(const char *cmd);

void exit(int code);

#endif
