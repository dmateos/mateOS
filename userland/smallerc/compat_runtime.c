#include "../syscalls.h"

typedef unsigned int size_t;
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EOF (-1)

typedef struct {
    int fd;
} mate_file_t;

static int c_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int c_isdigit(int c) {
    return c >= '0' && c <= '9';
}

static int c_isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int c_isalnum(int c) {
    return c_isalpha(c) || c_isdigit(c);
}

static unsigned int align8(unsigned int n) {
    return (n + 7u) & ~7u;
}

int atoi(const char *s) {
    int sign = 1;
    int v = 0;
    while (*s && c_isspace((unsigned char)*s)) s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (c_isdigit((unsigned char)*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}

int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return NULL;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        size_t i;
        for (i = 0; i < n; i++) d[i] = s[i];
    } else {
        size_t i;
        for (i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

int isspace(int c) { return c_isspace(c); }
int isdigit(int c) { return c_isdigit(c); }
int isalpha(int c) { return c_isalpha(c); }
int isalnum(int c) { return c_isalnum(c); }

typedef struct {
    unsigned int size;
} alloc_hdr_t;

void *malloc(size_t n) {
    if (n == 0) return NULL;
    unsigned int need = align8((unsigned int)n + (unsigned int)sizeof(alloc_hdr_t));
    alloc_hdr_t *h = (alloc_hdr_t *)sbrk((int)need);
    if ((unsigned int)h == 0xFFFFFFFFu) return NULL;
    h->size = (unsigned int)n;
    return (void *)(h + 1);
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
    unsigned int copy = h->size < (unsigned int)n ? h->size : (unsigned int)n;
    memcpy(np, p, copy);
    return np;
}

void *calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

static int write_all(int fd, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = write(fd, buf + off, (unsigned int)(len - off));
        if (n <= 0) return -1;
        off += n;
    }
    return off;
}

// File-descriptor writes for stdio FILE* streams must use SYS_FWRITE,
// not SYS_WRITE (which is console/stdout in mateOS).
static int fwrite_all_fd(int fd, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = fd_write(fd, buf + off, (unsigned int)(len - off));
        if (n <= 0) return -1;
        off += n;
    }
    return off;
}

void *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    if (!mode || !mode[0]) return NULL;
    if (mode[0] == 'r') flags = O_RDONLY;
    else if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT;
    else return NULL;

    int fd = open(path, flags);
    if (fd < 0) return NULL;
    if (mode[0] == 'a') seek(fd, 0, SEEK_END);

    mate_file_t *f = (mate_file_t *)malloc(sizeof(mate_file_t));
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    return (void *)f;
}

int fclose(void *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    if (!f) return -1;
    int rc = close(f->fd);
    free(f);
    return rc;
}

int fgetc(void *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    unsigned char ch;
    if (!f) return EOF;
    int n = fd_read(f->fd, &ch, 1);
    if (n <= 0) return EOF;
    return (int)ch;
}

int fputc(int ch, void *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    unsigned char c = (unsigned char)ch;
    if (!f) return EOF;
    if (fd_write(f->fd, &c, 1) != 1) return EOF;
    return c;
}

int puts(const char *s) {
    if (write_all(1, s, strlen(s)) < 0) return EOF;
    if (write_all(1, "\n", 1) < 0) return EOF;
    return 0;
}

int fputs(const char *s, void *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    if (!f) return EOF;
    return fwrite_all_fd(f->fd, s, strlen(s)) < 0 ? EOF : 0;
}

int putchar(int ch) {
    unsigned char c = (unsigned char)ch;
    return write(1, &c, 1) == 1 ? ch : EOF;
}

static int utoa_base(unsigned int v, unsigned int base, int upper, char *out) {
    static const char *digs_l = "0123456789abcdef";
    static const char *digs_u = "0123456789ABCDEF";
    const char *digs = upper ? digs_u : digs_l;
    char tmp[16];
    int i = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }
    while (v && i < (int)sizeof(tmp)) {
        tmp[i++] = digs[v % base];
        v /= base;
    }
    int n = i;
    while (i > 0) *out++ = tmp[--i];
    *out = '\0';
    return n;
}

static int mini_vsnprintf(char *out, int cap, const char *fmt, va_list ap) {
    int p = 0;
    const char *s = fmt;
    if (cap <= 0) return 0;
    while (*s) {
        if (*s != '%') {
            if (p + 1 < cap) out[p] = *s;
            p++;
            s++;
            continue;
        }
        s++;
        if (*s == '%') {
            if (p + 1 < cap) out[p] = '%';
            p++;
            s++;
            continue;
        }

        int left = 0;
        int width = 0;
        if (*s == '-') {
            left = 1;
            s++;
        }
        while (c_isdigit((unsigned char)*s)) {
            width = width * 10 + (*s - '0');
            s++;
        }

        char numbuf[32];
        const char *arg = numbuf;
        int len = 0;

        if (*s == 's') {
            arg = va_arg(ap, const char *);
            if (!arg) arg = "(null)";
            len = strlen(arg);
            s++;
        } else if (*s == 'c') {
            numbuf[0] = (char)va_arg(ap, int);
            numbuf[1] = '\0';
            len = 1;
            s++;
        } else if (*s == 'd' || *s == 'i') {
            int v = va_arg(ap, int);
            unsigned int uv;
            if (v < 0) {
                numbuf[0] = '-';
                uv = (unsigned int)(-v);
                len = 1 + utoa_base(uv, 10, 0, numbuf + 1);
            } else {
                uv = (unsigned int)v;
                len = utoa_base(uv, 10, 0, numbuf);
            }
            s++;
        } else if (*s == 'u') {
            unsigned int v = va_arg(ap, unsigned int);
            len = utoa_base(v, 10, 0, numbuf);
            s++;
        } else if (*s == 'x' || *s == 'X') {
            unsigned int v = va_arg(ap, unsigned int);
            len = utoa_base(v, 16, *s == 'X', numbuf);
            s++;
        } else if (*s == 'p') {
            unsigned int v = (unsigned int)va_arg(ap, void *);
            numbuf[0] = '0';
            numbuf[1] = 'x';
            len = 2 + utoa_base(v, 16, 0, numbuf + 2);
            s++;
        } else {
            if (p + 1 < cap) out[p] = '%';
            p++;
            if (*s) {
                if (p + 1 < cap) out[p] = *s;
                p++;
                s++;
            }
            continue;
        }

        int pad = width > len ? width - len : 0;
        if (!left) {
            while (pad-- > 0) {
                if (p + 1 < cap) out[p] = ' ';
                p++;
            }
        }
        for (int i = 0; i < len; i++) {
            if (p + 1 < cap) out[p] = arg[i];
            p++;
        }
        if (left) {
            while (pad-- > 0) {
                if (p + 1 < cap) out[p] = ' ';
                p++;
            }
        }
    }
    if (p < cap) out[p] = '\0';
    else out[cap - 1] = '\0';
    return p;
}

int vsprintf(char *dst, const char *fmt, va_list ap) {
    return mini_vsnprintf(dst, 0x7fffffff, fmt, ap);
}

int sprintf(char *dst, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = mini_vsnprintf(dst, 0x7fffffff, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(void *stream, const char *fmt, va_list ap) {
    mate_file_t *f = (mate_file_t *)stream;
    char buf[1024];
    int n = mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    if (!f) return EOF;
    if (fwrite_all_fd(f->fd, buf, strlen(buf)) < 0) return EOF;
    return n;
}

int fprintf(void *stream, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) {
    char buf[1024];
    int n = mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    if (write_all(1, buf, strlen(buf)) < 0) return EOF;
    return n;
}

int printf(const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int fgetpos(void *stream, void *pos) {
    mate_file_t *f = (mate_file_t *)stream;
    if (!f || !pos) return -1;
    int off = seek(f->fd, 0, SEEK_CUR);
    if (off < 0) return -1;
    memset(pos, 0, sizeof(int));
    memcpy(pos, &off, sizeof(int));
    return 0;
}

int fsetpos(void *stream, const void *pos) {
    mate_file_t *f = (mate_file_t *)stream;
    int off = 0;
    if (!f || !pos) return -1;
    memcpy(&off, pos, sizeof(int));
    return seek(f->fd, off, SEEK_SET) < 0 ? -1 : 0;
}
