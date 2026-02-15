#include "lib.h"
#include "arch/i686/legacytty.h"

#define KLOG_MAX_LINES 1024
#define KLOG_LINE_MAX 160

static char klog_lines[KLOG_MAX_LINES][KLOG_LINE_MAX];
static uint16_t klog_line_lens[KLOG_MAX_LINES];
static uint32_t klog_head = 0;   // Next slot to write.
static uint32_t klog_count = 0;  // Number of committed lines in ring.
static char klog_pending[KLOG_LINE_MAX];
static uint16_t klog_pending_len = 0;

size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len]) {
    len++;
  }
  return len;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
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

int strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (s1[i] != s2[i] || s1[i] == '\0') {
      return (unsigned char)s1[i] - (unsigned char)s2[i];
    }
  }
  return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i]; i++) {
    dest[i] = src[i];
  }
  for (; i < n; i++) {
    dest[i] = '\0';
  }
  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *a = s1;
  const unsigned char *b = s2;
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
  }
  return 0;
}

void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *d = dest;
  const unsigned char *s = src;
  if (d < s) {
    while (n--) *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n--) *--d = *--s;
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

typedef void (*putc_fn_t)(char);

static void emit_cstr(putc_fn_t out, const char *s) {
  if (!s) return;
  while (*s) out(*s++);
}

static void term_putc_adapter(char c) {
  term_putchar(c);
}

static void klog_commit_line(void) {
  uint32_t slot = klog_head;
  if (klog_pending_len >= KLOG_LINE_MAX) {
    klog_pending_len = KLOG_LINE_MAX - 1;
  }
  memcpy(klog_lines[slot], klog_pending, klog_pending_len);
  klog_lines[slot][klog_pending_len] = '\0';
  klog_line_lens[slot] = klog_pending_len;

  klog_head = (klog_head + 1) % KLOG_MAX_LINES;
  if (klog_count < KLOG_MAX_LINES) {
    klog_count++;
  }
  klog_pending_len = 0;
}

static void klog_putc(char c) {
  if (c == '\r') return;
  if (c == '\n') {
    klog_commit_line();
    return;
  }
  if (klog_pending_len < (KLOG_LINE_MAX - 1)) {
    klog_pending[klog_pending_len++] = c;
  }
}

static void kvprintf(putc_fn_t out, const char *format, va_list args) {
  va_list ap;
  va_copy(ap, args);
  while (*format) {
    if (*format == '%') {
      format++;
      switch (*format) {
      case 's': {
        const char *str = va_arg(ap, const char *);
        emit_cstr(out, str);
        break;
      }
      case 'c': {
        char c = va_arg(ap, int);
        out(c);
        break;
      }
      case 'd': {
        int num = va_arg(ap, int);
        char buf[32];
        itoa(num, buf, 10);
        emit_cstr(out, buf);
        break;
      }
      case 'x': {
        int num = va_arg(ap, int);
        char buf[32];
        itoa(num, buf, 16);
        emit_cstr(out, buf);
        break;
      }
      case 'p': {
        void *ptr = va_arg(ap, void *);
        char buf[32];
        itoa((int)ptr, buf, 16);
        emit_cstr(out, buf);
        break;
      }
      case '%': {
        out('%');
        break;
      }
      default:
        out('%');
        if (*format) out(*format);
        break;
      }
    } else {
      out(*format);
    }
    format++;
  }
  va_end(ap);
}

// This is also unsafe
void printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  kvprintf(term_putc_adapter, format, args);
  va_end(args);
}

// Kernel log: writes to terminal (if available) and kdebug ring.
void kprintf(const char *format, ...) {
  va_list args;
  va_list args_copy;
  va_start(args, format);
  va_copy(args_copy, args);
  kvprintf(term_putc_adapter, format, args);
  kvprintf(klog_putc, format, args_copy);
  va_end(args_copy);
  va_end(args);
}

uint32_t klog_snapshot_size(void) {
  uint32_t total = 0;
  uint32_t start = (klog_count == KLOG_MAX_LINES) ? klog_head : 0;
  for (uint32_t i = 0; i < klog_count; i++) {
    uint32_t idx = (start + i) % KLOG_MAX_LINES;
    total += (uint32_t)klog_line_lens[idx] + 1;  // include newline
  }
  total += klog_pending_len;
  return total;
}

int klog_read_bytes(uint32_t offset, void *buf, uint32_t len) {
  if (!buf || len == 0) return 0;

  uint8_t *dst = (uint8_t *)buf;
  uint32_t copied = 0;
  uint32_t pos = 0;
  uint32_t start = (klog_count == KLOG_MAX_LINES) ? klog_head : 0;

  for (uint32_t i = 0; i < klog_count; i++) {
    uint32_t idx = (start + i) % KLOG_MAX_LINES;
    uint32_t line_len = (uint32_t)klog_line_lens[idx];
    uint32_t chunk_len = line_len + 1; // newline

    if (offset < pos + chunk_len) {
      uint32_t in = offset > pos ? (offset - pos) : 0;
      while (in < chunk_len && copied < len) {
        if (in < line_len) {
          dst[copied++] = (uint8_t)klog_lines[idx][in];
        } else {
          dst[copied++] = (uint8_t)'\n';
        }
        in++;
      }
      if (copied >= len) return (int)copied;
    }
    pos += chunk_len;
  }

  if (klog_pending_len > 0 && copied < len) {
    if (offset < pos + klog_pending_len) {
      uint32_t in = offset > pos ? (offset - pos) : 0;
      while (in < klog_pending_len && copied < len) {
        dst[copied++] = (uint8_t)klog_pending[in++];
      }
    }
  }

  return (int)copied;
}
