#include "lib.h"
#include "arch/i686/legacytty.h"

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

// This is total shit and unsafe, does no bounds checks on buf etc
void itoa(int num, char *buf, int base) {
  if (num == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  int i = 0;
  bool is_negative = false;
  if (num < 0 && base == 10) {
    is_negative = true;
    num = -num;
  }

  while (num != 0) {
    int rem = num % base;
    buf[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
    num /= base;
  }

  if (is_negative) {
    buf[i++] = '-';
  }

  buf[i] = '\0';

  int j = 0;
  int k = i - 1;
  while (j < k) {
    char tmp = buf[j];
    buf[j] = buf[k];
    buf[k] = tmp;
    j++;
    k--;
  }
}

// This is also unsafe
void printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  while (*format) {
    if (*format == '%') {
      format++;
      switch (*format) {
      case 's': {
        const char *str = va_arg(args, const char *);
        while (*str) {
          term_putchar(*str++);
        }
        break;
      }
      case 'c': {
        char c = va_arg(args, int);
        term_putchar(c);
        break;
      }
      case 'd': {
        int num = va_arg(args, int);
        char buf[32];
        itoa(num, buf, 10);
        printf(buf);
        break;
      }
      case 'x': {
        int num = va_arg(args, int);
        char buf[32];
        itoa(num, buf, 16);
        printf(buf);
        break;
      }
      case 'p': {
        void *ptr = va_arg(args, void *);
        char buf[32];
        itoa((int)ptr, buf, 16);
        printf(buf);
        break;
      }
      case '%': {
        term_putchar('%');
        break;
      }
      }
    } else {
      term_putchar(*format);
    }
    format++;
  }
  va_end(args);
}