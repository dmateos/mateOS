#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int errno = 0;

#define SYS_WRITE    1
#define SYS_EXIT     2
#define SYS_SLEEPMS  27
#define SYS_OPEN     36
#define SYS_FREAD    37
#define SYS_FWRITE   38
#define SYS_CLOSE    39
#define SYS_SEEK     40
#define SYS_UNLINK   43
#define SYS_GETTICKS 45
#define SYS_SBRK     51

static inline int sc0(unsigned int n) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

static inline int sc1(unsigned int n, unsigned int a1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

static inline int sc2(unsigned int n, unsigned int a1, unsigned int a2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline int sc3(unsigned int n, unsigned int a1, unsigned int a2, unsigned int a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static int k_write(int fd, const void *buf, unsigned int len) {
    return sc3(SYS_WRITE, (unsigned int)fd, (unsigned int)buf, len);
}

void exit(int code) {
    (void)sc1(SYS_EXIT, (unsigned int)code);
    for (;;) { __asm__ volatile("hlt"); }
}

static int k_open(const char *path, int flags) {
    return sc2(SYS_OPEN, (unsigned int)path, (unsigned int)flags);
}

static int k_read_fd(int fd, void *buf, unsigned int len) {
    return sc3(SYS_FREAD, (unsigned int)fd, (unsigned int)buf, len);
}

static int k_write_fd(int fd, const void *buf, unsigned int len) {
    return sc3(SYS_FWRITE, (unsigned int)fd, (unsigned int)buf, len);
}

static int k_close(int fd) {
    return sc1(SYS_CLOSE, (unsigned int)fd);
}

static int k_seek(int fd, int off, int whence) {
    return sc3(SYS_SEEK, (unsigned int)fd, (unsigned int)off, (unsigned int)whence);
}

static int k_unlink(const char *path) {
    return sc1(SYS_UNLINK, (unsigned int)path);
}

static int k_sleep_ms(unsigned int ms) {
    return sc1(SYS_SLEEPMS, ms);
}

static unsigned int k_ticks(void) {
    return (unsigned int)sc0(SYS_GETTICKS);
}

static void *k_sbrk(int increment) {
    return (void *)sc1(SYS_SBRK, (unsigned int)increment);
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if (c == 0) return (char *)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        size_t i = 0;
        while (n[i] && h[i] == n[i]) i++;
        if (!n[i]) return (char *)h;
    }
    return NULL;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isprint(int c) { return c >= 32 && c <= 126; }

int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (a[i] == '\0') return 0;
    }
    return 0;
}

typedef struct {
    size_t sz;
} alloc_hdr_t;

static size_t align8(size_t v) { return (v + 7u) & ~7u; }

void *malloc(size_t n) {
    size_t need;
    alloc_hdr_t *h;
    if (n == 0) return NULL;
    need = align8(n + sizeof(alloc_hdr_t));
    h = (alloc_hdr_t *)k_sbrk((int)need);
    if ((uintptr_t)h == 0xFFFFFFFFu) return NULL;
    h->sz = n;
    return (void *)(h + 1);
}

void *calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *p) {
    (void)p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) return NULL;
    alloc_hdr_t *h = ((alloc_hdr_t *)p) - 1;
    void *np = malloc(n);
    if (!np) return NULL;
    size_t copy = h->sz < n ? h->sz : n;
    memcpy(np, p, copy);
    return np;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (!d) return NULL;
    memcpy(d, s, n);
    return d;
}

int abs(int x) { return x < 0 ? -x : x; }

int atoi(const char *s) {
    int sign = 1, v = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}

double atof(const char *s) {
    int sign = 1;
    double v = 0.0, frac = 0.0, base = 0.1;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) { v = v * 10.0 + (double)(*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) {
            frac += (double)(*s - '0') * base;
            base *= 0.1;
            s++;
        }
    }
    return (v + frac) * (double)sign;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int system(const char *cmd) {
    (void)cmd;
    return -1;
}

unsigned int usleep(unsigned int usec) {
    unsigned int ms = (usec + 999u) / 1000u;
    k_sleep_ms(ms);
    return 0;
}

int access(const char *path, int mode) {
    (void)mode;
    int fd = k_open(path, O_RDONLY);
    if (fd < 0) return -1;
    k_close(fd);
    return 0;
}

int mkdir(const char *path, int mode) {
    (void)path;
    (void)mode;
    return 0;
}

static FILE g_stdin = {0, 0, 0, 0, 0, 1};
static FILE g_stdout = {1, 0, 0, 0, 1, 1};
static FILE g_stderr = {2, 0, 0, 0, 1, 1};
FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

static int mode_to_flags(const char *m) {
    if (!m || !m[0]) return O_RDONLY;
    if (m[0] == 'r') return O_RDONLY;
    if (m[0] == 'w') return O_WRONLY | O_CREAT | O_TRUNC;
    if (m[0] == 'a') return O_WRONLY | O_CREAT;
    return O_RDONLY;
}

FILE *fopen(const char *path, const char *mode) {
    int flags = mode_to_flags(mode);
    int fd = k_open(path, flags);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        k_close(fd);
        return NULL;
    }
    f->fd = fd;
    f->pos = 0;
    f->eof = 0;
    f->err = 0;
    f->mode = flags;
    f->is_stdio = 0;

    if (mode && mode[0] == 'a') {
        int p = k_seek(fd, 0, SEEK_END);
        if (p >= 0) f->pos = p;
    }

    return f;
}

int fclose(FILE *f) {
    if (!f) return -1;
    if (!f->is_stdio) {
        int rc = k_close(f->fd);
        free(f);
        return rc;
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    unsigned int total = (unsigned int)(size * nmemb);
    int n = k_read_fd(f->fd, ptr, total);
    if (n <= 0) {
        f->eof = 1;
        return 0;
    }
    f->pos += n;
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    unsigned int total = (unsigned int)(size * nmemb);
    int n;
    if (f->fd <= 2) n = k_write(f->fd, ptr, total);
    else n = k_write_fd(f->fd, ptr, total);
    if (n < 0) {
        f->err = 1;
        return 0;
    }
    f->pos += n;
    return (size_t)n / size;
}

int fseek(FILE *f, long off, int whence) {
    if (!f) return -1;
    int p = k_seek(f->fd, (int)off, whence);
    if (p < 0) {
        f->err = 1;
        return -1;
    }
    f->pos = p;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    if (!f) return -1;
    int p = k_seek(f->fd, 0, SEEK_CUR);
    if (p < 0) return f->pos;
    f->pos = p;
    return f->pos;
}

int fflush(FILE *f) { (void)f; return 0; }
int feof(FILE *f) { return f ? f->eof : 1; }
int ferror(FILE *f) { return f ? f->err : 1; }
void clearerr(FILE *f) { if (f) { f->eof = 0; f->err = 0; } }

int fileno(FILE *f) { return f ? f->fd : -1; }
int isatty(int fd) { (void)fd; return 0; }

int remove(const char *path) {
    return k_unlink(path);
}

int rename(const char *oldpath, const char *newpath) {
    int in = k_open(oldpath, O_RDONLY);
    if (in < 0) return -1;
    int out = k_open(newpath, O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) {
        k_close(in);
        return -1;
    }

    unsigned char buf[1024];
    while (1) {
        int n = k_read_fd(in, buf, sizeof(buf));
        if (n <= 0) break;
        int w = k_write_fd(out, buf, (unsigned int)n);
        if (w != n) {
            k_close(in);
            k_close(out);
            return -1;
        }
    }

    k_close(in);
    k_close(out);
    k_unlink(oldpath);
    return 0;
}

int fgetc(FILE *f) {
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return EOF;
    return (int)c;
}

int fputc(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, f) != 1) return EOF;
    return c;
}

int putchar(int c) {
    unsigned char ch = (unsigned char)c;
    if (k_write(1, &ch, 1) < 0) return EOF;
    return c;
}

int puts(const char *s) {
    size_t n = strlen(s);
    if (k_write(1, s, (unsigned int)n) < 0) return EOF;
    if (k_write(1, "\n", 1) < 0) return EOF;
    return (int)(n + 1);
}

static void out_char(char **dst, size_t *left, int *count, char c) {
    if (*left > 1) {
        **dst = c;
        (*dst)++;
        (*left)--;
    }
    (*count)++;
}

static void out_repeat(char **dst, size_t *left, int *count, char c, int n) {
    while (n-- > 0) out_char(dst, left, count, c);
}

static void out_str(char **dst, size_t *left, int *count, const char *s) {
    if (!s) s = "(null)";
    while (*s) out_char(dst, left, count, *s++);
}

static void out_uint(char **dst, size_t *left, int *count, unsigned int v, unsigned base, int upper) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) {
        out_char(dst, left, count, '0');
        return;
    }
    while (v && i < (int)sizeof(buf)) {
        buf[i++] = digits[v % base];
        v /= base;
    }
    while (i > 0) out_char(dst, left, count, buf[--i]);
}

static void out_int(char **dst, size_t *left, int *count, int v) {
    if (v < 0) {
        out_char(dst, left, count, '-');
        out_uint(dst, left, count, (unsigned int)(-v), 10, 0);
    } else {
        out_uint(dst, left, count, (unsigned int)v, 10, 0);
    }
}

static void out_float(char **dst, size_t *left, int *count, double d) {
    if (d < 0) {
        out_char(dst, left, count, '-');
        d = -d;
    }
    int iv = (int)d;
    double frac = d - (double)iv;
    out_uint(dst, left, count, (unsigned int)iv, 10, 0);
    out_char(dst, left, count, '.');
    for (int i = 0; i < 3; i++) {
        frac *= 10.0;
        int digit = (int)frac;
        out_char(dst, left, count, (char)('0' + digit));
        frac -= digit;
    }
}

static int fmt_uint_buf(char *buf, size_t cap, unsigned int v, unsigned base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char rev[32];
    int i = 0, n = 0;
    if (cap == 0) return 0;
    if (v == 0) {
        if (cap > 1) buf[0] = '0';
        if (cap > 0) buf[1 < (int)cap ? 1 : 0] = '\0';
        return 1;
    }
    while (v && i < (int)sizeof(rev)) {
        rev[i++] = digits[v % base];
        v /= base;
    }
    while (i > 0 && n < (int)cap - 1) {
        buf[n++] = rev[--i];
    }
    buf[n] = '\0';
    return n;
}

static int fmt_int_buf(char *buf, size_t cap, int v) {
    if (cap == 0) return 0;
    if (v < 0) {
        if (cap < 3) { buf[0] = '\0'; return 0; }
        buf[0] = '-';
        return 1 + fmt_uint_buf(buf + 1, cap - 1, (unsigned int)(-v), 10, 0);
    }
    return fmt_uint_buf(buf, cap, (unsigned int)v, 10, 0);
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    char *dst = buf;
    size_t left = n;
    int count = 0;

    if (n == 0) {
        static char dummy;
        dst = &dummy;
        left = 0;
    }

    while (*fmt) {
        if (*fmt != '%') {
            out_char(&dst, &left, &count, *fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '%') {
            out_char(&dst, &left, &count, '%');
            fmt++;
            continue;
        }

        {
            int zero_pad = 0;
            int left_align = 0;
            int min_width = 0;
            int have_precision = 0;
            int precision = 0;

            while (*fmt == '0' || *fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
                if (*fmt == '0') zero_pad = 1;
                if (*fmt == '-') left_align = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                min_width = min_width * 10 + (*fmt - '0');
                fmt++;
            }
        if (*fmt == '.') {
            have_precision = 1;
            precision = 0;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't') fmt++;

        switch (*fmt) {
            case 'd':
            case 'i':
            {
                char nb[64];
                int v = va_arg(ap, int);
                int nlen = fmt_int_buf(nb, sizeof(nb), v);
                int neg = (nb[0] == '-');
                int digits_len = nlen - (neg ? 1 : 0);
                int zeroes = 0;
                if (have_precision && precision > digits_len) zeroes = precision - digits_len;
                {
                    int eff_len = nlen + zeroes;
                    int pad = (min_width > eff_len) ? (min_width - eff_len) : 0;
                    char padch = (zero_pad && !left_align && !have_precision) ? '0' : ' ';
                    if (!left_align) {
                        if (padch == '0' && neg) {
                            out_char(&dst, &left, &count, '-');
                            out_repeat(&dst, &left, &count, '0', pad + zeroes);
                            out_str(&dst, &left, &count, nb + 1);
                        } else {
                            out_repeat(&dst, &left, &count, padch, pad);
                            if (neg) {
                                out_char(&dst, &left, &count, '-');
                                out_repeat(&dst, &left, &count, '0', zeroes);
                                out_str(&dst, &left, &count, nb + 1);
                            } else {
                                out_repeat(&dst, &left, &count, '0', zeroes);
                                out_str(&dst, &left, &count, nb);
                            }
                        }
                    } else {
                        if (neg) {
                            out_char(&dst, &left, &count, '-');
                            out_repeat(&dst, &left, &count, '0', zeroes);
                            out_str(&dst, &left, &count, nb + 1);
                        } else {
                            out_repeat(&dst, &left, &count, '0', zeroes);
                            out_str(&dst, &left, &count, nb);
                        }
                        out_repeat(&dst, &left, &count, ' ', pad);
                    }
                }
                break;
            }
            case 'u':
            {
                char nb[64];
                int nlen = fmt_uint_buf(nb, sizeof(nb), va_arg(ap, unsigned int), 10, 0);
                int zeroes = (have_precision && precision > nlen) ? (precision - nlen) : 0;
                int pad = (min_width > (nlen + zeroes)) ? (min_width - (nlen + zeroes)) : 0;
                char padch = (zero_pad && !left_align && !have_precision) ? '0' : ' ';
                if (!left_align) out_repeat(&dst, &left, &count, padch, pad);
                out_repeat(&dst, &left, &count, '0', zeroes);
                out_str(&dst, &left, &count, nb);
                if (left_align) out_repeat(&dst, &left, &count, ' ', pad);
                break;
            }
            case 'x':
            {
                char nb[64];
                int nlen = fmt_uint_buf(nb, sizeof(nb), va_arg(ap, unsigned int), 16, 0);
                int zeroes = (have_precision && precision > nlen) ? (precision - nlen) : 0;
                int pad = (min_width > (nlen + zeroes)) ? (min_width - (nlen + zeroes)) : 0;
                char padch = (zero_pad && !left_align && !have_precision) ? '0' : ' ';
                if (!left_align) out_repeat(&dst, &left, &count, padch, pad);
                out_repeat(&dst, &left, &count, '0', zeroes);
                out_str(&dst, &left, &count, nb);
                if (left_align) out_repeat(&dst, &left, &count, ' ', pad);
                break;
            }
            case 'X':
            {
                char nb[64];
                int nlen = fmt_uint_buf(nb, sizeof(nb), va_arg(ap, unsigned int), 16, 1);
                int zeroes = (have_precision && precision > nlen) ? (precision - nlen) : 0;
                int pad = (min_width > (nlen + zeroes)) ? (min_width - (nlen + zeroes)) : 0;
                char padch = (zero_pad && !left_align && !have_precision) ? '0' : ' ';
                if (!left_align) out_repeat(&dst, &left, &count, padch, pad);
                out_repeat(&dst, &left, &count, '0', zeroes);
                out_str(&dst, &left, &count, nb);
                if (left_align) out_repeat(&dst, &left, &count, ' ', pad);
                break;
            }
            case 'c':
                out_char(&dst, &left, &count, (char)va_arg(ap, int));
                break;
            case 's':
                out_str(&dst, &left, &count, va_arg(ap, const char *));
                break;
            case 'p':
                out_str(&dst, &left, &count, "0x");
                out_uint(&dst, &left, &count, (unsigned int)(uintptr_t)va_arg(ap, void *), 16, 0);
                break;
            case 'f':
                out_float(&dst, &left, &count, va_arg(ap, double));
                break;
            default:
                out_char(&dst, &left, &count, '?');
                break;
        }
        } /* width/flag parsing scope */

        if (*fmt) fmt++;
    }

    if (n > 0) {
        if (left > 0) *dst = '\0';
        else buf[n - 1] = '\0';
    }

    return count;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (n < 0) return n;
    size_t len = strlen(tmp);
    fwrite(tmp, 1, len, f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}

static int parse_int_base(const char **ps, int base, int *out) {
    const char *s = *ps;
    while (isspace((unsigned char)*s)) s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    int v = 0;
    int any = 0;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        if (d < 0 || d >= base) break;
        v = v * base + d;
        any = 1;
        s++;
    }
    if (!any) return 0;
    *out = sign * v;
    *ps = s;
    return 1;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int matched = 0;
    const char *s = str;
    const char *f = fmt;

    while (*f) {
        if (*f == '%') {
            f++;
            int *out = va_arg(ap, int *);
            int ok = 0;
            if (*f == 'd' || *f == 'i') {
                ok = parse_int_base(&s, 10, out);
            } else if (*f == 'x' || *f == 'X') {
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
                ok = parse_int_base(&s, 16, out);
            } else if (*f == 'o') {
                ok = parse_int_base(&s, 8, out);
            }
            if (!ok) break;
            matched++;
            f++;
            continue;
        }

        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) f++;
            while (isspace((unsigned char)*s)) s++;
            continue;
        }

        if (*s != *f) break;
        s++;
        f++;
    }

    va_end(ap);
    return matched;
}

static const double PI = 3.14159265358979323846;

double fabs(double x) { return x < 0.0 ? -x : x; }

double sin(double x) {
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0);
}

double cos(double x) {
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;
    double x2 = x * x;
    return 1.0 - x2 / 2.0 + (x2 * x2) / 24.0;
}

double tan(double x) {
    double c = cos(x);
    if (c > -1e-6 && c < 1e-6) return 0.0;
    return sin(x) / c;
}

double atan(double x) {
    if (x > 1.0) return PI / 2.0 - atan(1.0 / x);
    if (x < -1.0) return -PI / 2.0 - atan(1.0 / x);
    double x2 = x * x;
    return x * (1.0 - x2 / 3.0 + x2 * x2 / 5.0);
}

// Minimal compiler-rt replacements for 64-bit division in freestanding i386.
static unsigned long long udivmod64(unsigned long long n, unsigned long long d,
                                    unsigned long long *rem_out) {
    if (d == 0) {
        if (rem_out) *rem_out = 0;
        return 0;
    }

    unsigned long long q = 0;
    unsigned long long r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1ull);
        if (r >= d) {
            r -= d;
            q |= (1ull << i);
        }
    }
    if (rem_out) *rem_out = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long n, unsigned long long d) {
    return udivmod64(n, d, 0);
}

unsigned long long __umoddi3(unsigned long long n, unsigned long long d) {
    unsigned long long r = 0;
    (void)udivmod64(n, d, &r);
    return r;
}

long long __divdi3(long long n, long long d) {
    int neg = 0;
    unsigned long long un, ud, uq;

    if (n < 0) { un = (unsigned long long)(-n); neg ^= 1; }
    else un = (unsigned long long)n;
    if (d < 0) { ud = (unsigned long long)(-d); neg ^= 1; }
    else ud = (unsigned long long)d;

    uq = __udivdi3(un, ud);
    return neg ? -(long long)uq : (long long)uq;
}

long long __moddi3(long long n, long long d) {
    int neg = (n < 0);
    unsigned long long un = (unsigned long long)(n < 0 ? -n : n);
    unsigned long long ud = (unsigned long long)(d < 0 ? -d : d);
    unsigned long long ur = __umoddi3(un, ud);
    return neg ? -(long long)ur : (long long)ur;
}
