#ifndef _LIB_H
#define _LIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *str);
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);

#endif
