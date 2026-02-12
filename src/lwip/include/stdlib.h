#ifndef _COMPAT_STDLIB_H
#define _COMPAT_STDLIB_H

/* atoi - simple decimal parser */
static inline int atoi(const char *s) {
  int n = 0, neg = 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
  return neg ? -n : n;
}

#endif
