#ifndef _SMALLERC_STRING_H
#define _SMALLERC_STRING_H

#include <stddef.h>

size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strchr(const char *s, int c);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#endif
