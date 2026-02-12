#ifndef _COMPAT_STDIO_H
#define _COMPAT_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Minimal snprintf for lwIP debug messages */
static inline int snprintf(char *buf, size_t size, const char *fmt, ...) {
  (void)fmt;
  if (size > 0) buf[0] = '\0';
  return 0;
}

#endif
