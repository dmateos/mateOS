#ifndef _LIB_H
#define _LIB_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
void printf(const char *format, ...);
void itoa(int num, char *buf, int base);

#endif
