#include "lib.h"

size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len]) {
    len++;
  }
  return len;
}

void *memset(void *ptr, int value, size_t num) {
  unsigned char *p = ptr;
  while (num--) {
    *p++ = (unsigned char)value;
  }
  return ptr;
}

void *memcpy(void *dest, const void *src, size_t num) {
  unsigned char *d = dest;
  const unsigned char *s = src;
  while (num--) {
    *d++ = *s++;
  }
  return dest;
}