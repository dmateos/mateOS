#include "libc.h"
#include "syscalls.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <semaphore.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/syscall.h>

int errno = 0;
char **environ = 0;

int *__errno_location(void) {
    return &errno;
}

typedef struct {
    unsigned int size;
} alloc_hdr_t;

typedef struct __mate_file {
    int fd;
    unsigned char owned;
} mate_file_t;

static mate_file_t g_stdin_obj = { 0, 0 };
static mate_file_t g_stdout_obj = { 1, 0 };
static mate_file_t g_stderr_obj = { 2, 0 };
FILE *stdin = (FILE *)&g_stdin_obj;
FILE *stdout = (FILE *)&g_stdout_obj;
FILE *stderr = (FILE *)&g_stderr_obj;

void *_GLOBAL_OFFSET_TABLE_;
void (*__init_array_start[])(void) = { 0 };
void (*__init_array_end[])(void) = { 0 };
void (*__fini_array_start[])(void) = { 0 };
void (*__fini_array_end[])(void) = { 0 };

static int c_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int c_isdigit(int c) {
    return c >= '0' && c <= '9';
}

static int c_isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static unsigned int align8(unsigned int n) {
    return (n + 7u) & ~7u;
}

size_t strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return (size_t)n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst + strlen(dst);
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    size_t i;
    for (i = 0; i < n && src[i]; i++) d[i] = src[i];
    d[i] = '\0';
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

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*a == *s) return (char *)s;
            a++;
        }
        s++;
    }
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
    size_t i;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (i = 0; i < n; i++) d[i] = s[i];
    } else {
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

static int parse_base_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    int sign = 1;
    unsigned long acc = 0;
    int d;
    while (*s && c_isspace((unsigned char)*s)) s++;
    if (*s == '-' || *s == '+') {
        if (*s == '-') sign = -1;
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0) {
        base = 10;
    }
    while ((d = parse_base_digit(*s)) >= 0 && d < base) {
        acc = acc * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return sign < 0 ? -(long)acc : (long)acc;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    long v = strtol(nptr, endptr, base);
    return (unsigned long)v;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    int d;
    while (*s && c_isspace((unsigned char)*s)) s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0) {
        base = 10;
    }
    while ((d = parse_base_digit(*s)) >= 0 && d < base) {
        acc = acc * (unsigned long long)base + (unsigned long long)d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return acc;
}

long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return strtoul(nptr, endptr, base);
}

unsigned long long __isoc23_strtoull(const char *nptr, char **endptr, int base) {
    return strtoull(nptr, endptr, base);
}

void *malloc(size_t n) {
    if (n == 0) {
        write(2, "[malloc(0)]\n", 12);
        return NULL;
    }
    {
        unsigned int need = align8((unsigned int)n + (unsigned int)sizeof(alloc_hdr_t));
        alloc_hdr_t *h = (alloc_hdr_t *)sbrk((int)need);
        if ((unsigned int)h == 0xFFFFFFFFu) {
            write(2, "[malloc fail: sbrk returned -1]\n", 32);
            return NULL;
        }
        h->size = (unsigned int)n;
        return (void *)(h + 1);
    }
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
    {
        alloc_hdr_t *h = ((alloc_hdr_t *)p) - 1;
        void *np = malloc(n);
        size_t copy;
        if (!np) return NULL;
        copy = h->size < n ? h->size : n;
        memcpy(np, p, copy);
        return np;
    }
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

static int fwrite_all_fd(int fd, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = fd_write(fd, buf + off, (unsigned int)(len - off));
        if (n <= 0) return -1;
        off += n;
    }
    return off;
}

FILE *fdopen(int fd, const char *mode) {
    mate_file_t *f;
    (void)mode;
    f = (mate_file_t *)malloc(sizeof(mate_file_t));
    if (!f) return NULL;
    f->fd = fd;
    f->owned = 0;
    return (FILE *)f;
}

FILE *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    int fd;
    mate_file_t *f;
    if (!mode || !mode[0]) return NULL;
    if (mode[0] == 'r') flags = O_RDONLY;
    else if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT;
    else return NULL;

    fd = open(path, flags);
    if (fd < 0) return NULL;
    if (mode[0] == 'a') seek(fd, 0, SEEK_END);

    f = (mate_file_t *)malloc(sizeof(mate_file_t));
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    f->owned = 1;
    return (FILE *)f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    FILE *nf = fopen(path, mode);
    if (!nf) return NULL;
    if (f) {
        if (f->owned) close(f->fd);
        f->fd = ((mate_file_t *)nf)->fd;
        f->owned = 1;
        free(nf);
        return stream;
    }
    return nf;
}

int fclose(FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    int rc;
    if (!f) return -1;
    rc = 0;
    if (f->owned) rc = close(f->fd);
    free(f);
    return rc;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int fgetc(FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    unsigned char ch;
    int n;
    if (!f) return EOF;
    n = fd_read(f->fd, &ch, 1);
    if (n <= 0) return EOF;
    return (int)ch;
}

char *fgets(char *s, int size, FILE *stream) {
    int i;
    if (!s || size <= 0 || !stream) return NULL;
    for (i = 0; i < size - 1; i++) {
        int ch = fgetc(stream);
        if (ch == EOF) break;
        s[i] = (char)ch;
        if (ch == '\n') {
            i++;
            break;
        }
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    size_t total;
    int n;
    if (!f || !ptr || size == 0 || nmemb == 0) return 0;
    total = size * nmemb;
    n = fd_read(f->fd, ptr, (unsigned int)total);
    if (n <= 0) return 0;
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    size_t total;
    int n;
    if (!f || !ptr || size == 0 || nmemb == 0) return 0;
    total = size * nmemb;
    n = fd_write(f->fd, ptr, (unsigned int)total);
    if (n <= 0) return 0;
    return (size_t)n / size;
}

int fputc(int ch, FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    unsigned char c = (unsigned char)ch;
    if (!f) return EOF;
    if (fd_write(f->fd, &c, 1) != 1) return EOF;
    return (int)c;
}

int puts(const char *s) {
    if (write_all(1, s, strlen(s)) < 0) return EOF;
    if (write_all(1, "\n", 1) < 0) return EOF;
    return 0;
}

int fputs(const char *s, FILE *stream) {
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
    int n;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }
    while (v && i < (int)sizeof(tmp)) {
        tmp[i++] = digs[v % base];
        v /= base;
    }
    n = i;
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

        {
            int left = 0;
            int width = 0;
            char numbuf[32];
            const char *arg = numbuf;
            int len = 0;
            int i;
            int pad;

            if (*s == '-') {
                left = 1;
                s++;
            }
            while (c_isdigit((unsigned char)*s)) {
                width = width * 10 + (*s - '0');
                s++;
            }

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

            pad = width > len ? width - len : 0;
            if (!left) {
                while (pad-- > 0) {
                    if (p + 1 < cap) out[p] = ' ';
                    p++;
                }
            }
            for (i = 0; i < len; i++) {
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

int vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap) {
    if (cap == 0) return mini_vsnprintf((char *)"", 1, fmt, ap);
    return mini_vsnprintf(dst, (int)cap, fmt, ap);
}

int snprintf(char *dst, size_t cap, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    mate_file_t *f = (mate_file_t *)stream;
    char buf[1024];
    int n;
    if (!f) return EOF;
    n = mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    if (f->fd == 1 || f->fd == 2) {
        if (write_all(f->fd, buf, strlen(buf)) < 0) return EOF;
    } else {
        if (fwrite_all_fd(f->fd, buf, strlen(buf)) < 0) return EOF;
    }
    return n;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int fseek(FILE *stream, long off, int whence) {
    mate_file_t *f = (mate_file_t *)stream;
    if (!f) return -1;
    return seek(f->fd, (int)off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *stream) {
    mate_file_t *f = (mate_file_t *)stream;
    if (!f) return -1;
    return (long)seek(f->fd, 0, SEEK_CUR);
}

int fgetpos(FILE *stream, fpos_t *pos) {
    long off = ftell(stream);
    if (!pos || off < 0) return -1;
    pos->off = (int)off;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos) {
    if (!pos) return -1;
    return fseek(stream, (long)pos->off, SEEK_SET);
}

int remove(const char *path) {
    return unlink(path);
}

char *getcwd(char *buf, size_t size) {
    const char *root = "/";
    if (!buf || size < 2) return NULL;
    buf[0] = root[0];
    buf[1] = '\0';
    return buf;
}

int access(const char *path, int mode) {
    stat_t st;
    (void)mode;
    return stat(path, &st) == 0 ? 0 : -1;
}

long sysconf(int name) {
    if (name == _SC_PAGESIZE) return 4096;
    return -1;
}

int read(int fd, void *buf, unsigned int len) {
    return fd_read(fd, buf, len);
}

int lseek(int fd, int offset, int whence) {
    return seek(fd, offset, whence);
}

int execvp(const char *file, char *const argv[]) {
    int argc = 0;
    while (argv && argv[argc]) argc++;
    return spawn_argv(file, (const char **)argv, argc);
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

char *realpath(const char *path, char *resolved_path) {
    if (!path) return NULL;
    if (!resolved_path) {
        size_t n = strlen(path) + 1;
        resolved_path = (char *)malloc(n);
        if (!resolved_path) return NULL;
    }
    strcpy(resolved_path, path);
    return resolved_path;
}

double strtod(const char *nptr, char **endptr) {
    return (double)strtol(nptr, endptr, 10);
}

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}

long double ldexpl(long double x, int exp) {
    if (exp > 0) {
        while (exp-- > 0) x *= 2.0;
    } else {
        while (exp++ < 0) x *= 0.5;
    }
    return x;
}

int gettimeofday(struct timeval *tv, void *tz) {
    unsigned int t;
    (void)tz;
    if (!tv) return -1;
    t = get_ticks();
    tv->tv_sec = (long)(t / 100);
    tv->tv_usec = (long)((t % 100) * 10000);
    return 0;
}

time_t time(time_t *out) {
    time_t now = (time_t)(get_ticks() / 100);
    if (out) *out = now;
    return now;
}

struct tm *localtime(const time_t *t) {
    static struct tm tmv;
    time_t sec;
    int daysec;
    if (!t) return NULL;
    sec = *t;
    memset(&tmv, 0, sizeof(tmv));
    daysec = (int)(sec % 86400);
    tmv.tm_hour = daysec / 3600;
    tmv.tm_min = (daysec % 3600) / 60;
    tmv.tm_sec = daysec % 60;
    tmv.tm_mday = 1;
    tmv.tm_mon = 0;
    tmv.tm_year = 70;
    return &tmv;
}

int sigemptyset(sigset_t *set) {
    if (!set) return -1;
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) return -1;
    *set = 0xFFFFFFFFu;
    return 0;
}

int sigaddset(sigset_t *set, int signo) {
    if (!set || signo < 0 || signo >= 32) return -1;
    *set |= (1u << (unsigned int)signo);
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    if (oldset) *oldset = 0;
    return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    return sigprocmask(how, set, oldset);
}

int _setjmp(jmp_buf env) {
    (void)env;
    return 0;
}

void longjmp(jmp_buf env, int val) {
    (void)env;
    exit(val ? val : 1);
}

int sigsetjmp(sigjmp_buf env, int savesigs) {
    (void)savesigs;
    return _setjmp(env);
}

void siglongjmp(sigjmp_buf env, int val) {
    longjmp(env, val);
}

int sem_init(sem_t *sem, int pshared, unsigned int value) {
    (void)pshared;
    if (!sem) return -1;
    sem->value = (int)value;
    return 0;
}

int sem_wait(sem_t *sem) {
    if (!sem) return -1;
    while (sem->value <= 0) yield();
    sem->value--;
    return 0;
}

int sem_post(sem_t *sem) {
    if (!sem) return -1;
    sem->value++;
    return 0;
}

#define MAX_PTHREAD_KEYS 32
static const void *g_pthread_key_values[MAX_PTHREAD_KEYS];
static unsigned char g_pthread_key_used[MAX_PTHREAD_KEYS];

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
    unsigned int i;
    (void)destructor;
    if (!key) return -1;
    for (i = 0; i < MAX_PTHREAD_KEYS; i++) {
        if (!g_pthread_key_used[i]) {
            g_pthread_key_used[i] = 1;
            g_pthread_key_values[i] = NULL;
            *key = i;
            return 0;
        }
    }
    return -1;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= MAX_PTHREAD_KEYS) return -1;
    g_pthread_key_used[key] = 0;
    g_pthread_key_values[key] = NULL;
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= MAX_PTHREAD_KEYS) return -1;
    g_pthread_key_values[key] = value;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= MAX_PTHREAD_KEYS) return NULL;
    if (!g_pthread_key_used[key]) return NULL;
    return (void *)g_pthread_key_values[key];
}

int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    (void)pshared;
    if (!lock) return -1;
    lock->lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock) {
    if (!lock) return -1;
    lock->lock = 0;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    if (!lock) return -1;
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        yield();
    }
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    if (!lock) return -1;
    return __sync_lock_test_and_set(&lock->lock, 1) ? -1 : 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
    if (!lock) return -1;
    __sync_lock_release(&lock->lock);
    return 0;
}

void *dlopen(const char *filename, int flags) {
    write(2, "[dlopen called]\n", 16);
    (void)filename;
    (void)flags;
    return NULL;
}

void *dlsym(void *handle, const char *symbol) {
    write(2, "[dlsym called]\n", 15);
    (void)handle;
    (void)symbol;
    return NULL;
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

char *dlerror(void) {
    return "dlopen unsupported";
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    return malloc(length);
}

int munmap(void *addr, size_t length) {
    (void)length;
    free(addr);
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

long syscall(long num, ...) {
    (void)num;
    errno = ENOSYS;
    return -1;
}

static unsigned short ctype_b[257];
static int ctype_upper[256];
static int ctype_lower[256];
static int ctype_inited;

static void ctype_init(void) {
    int i;
    if (ctype_inited) return;
    for (i = 0; i < 256; i++) {
        unsigned short f = 0;
        if (c_isspace(i)) f |= 0x20;
        if (c_isdigit(i)) f |= 0x04;
        if (c_isalpha(i)) f |= 0x01;
        ctype_b[i + 1] = f;
        if (i >= 'A' && i <= 'Z') {
            ctype_lower[i] = i + 32;
            ctype_upper[i] = i;
        } else if (i >= 'a' && i <= 'z') {
            ctype_upper[i] = i - 32;
            ctype_lower[i] = i;
        } else {
            ctype_upper[i] = i;
            ctype_lower[i] = i;
        }
    }
    ctype_inited = 1;
}

unsigned short **__ctype_b_loc(void) {
    static unsigned short *p = &ctype_b[1];
    ctype_init();
    return &p;
}

int **__ctype_toupper_loc(void) {
    static int *p = ctype_upper;
    ctype_init();
    return &p;
}

int **__ctype_tolower_loc(void) {
    static int *p = ctype_lower;
    ctype_init();
    return &p;
}

int __isoc99_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    int matched = 0;
    (void)ap;
    if (!str || !fmt) return 0;
    if (strcmp(fmt, "%d") == 0) {
        int *out;
        va_start(ap, fmt);
        out = va_arg(ap, int *);
        if (out) {
            char *end;
            *out = (int)strtol(str, &end, 10);
            if (end != str) matched = 1;
        }
        va_end(ap);
        return matched;
    }
    return 0;
}

char *strerror(int errnum) {
    (void)errnum;
    return "mateos error";
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    size_t i, j, k;
    unsigned char *b = (unsigned char *)base;
    if (!base || !compar || size == 0) return;
    for (i = 1; i < nmemb; i++) {
        for (j = i; j > 0; j--) {
            unsigned char *a = b + (j - 1) * size;
            unsigned char *c = b + j * size;
            if (compar(a, c) <= 0) break;
            for (k = 0; k < size; k++) {
                unsigned char t = a[k];
                a[k] = c[k];
                c[k] = t;
            }
        }
    }
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    return 0;
}

void __libc_freeres(void) {
}

void __rt_exit(int code) {
    exit(code);
}

void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func) {
    char buf[256];
    snprintf(buf, sizeof(buf), "assertion failed: %s (%s:%u %s)\n", expr, file, line, func ? func : "?");
    write(2, buf, (unsigned int)strlen(buf));
    exit(1);
}

// I/O helpers already used by current apps.
void print(const char *s) {
    write(1, s, (unsigned int)strlen(s));
}

void print_char(char c) {
    write(1, &c, 1);
}

void print_num(int n) {
    if (n < 0) {
        print_char('-');
        n = -n;
    }
    if (n == 0) {
        print_char('0');
        return;
    }
    {
        char buf[12];
        int i = 0;
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
        while (i > 0) print_char(buf[--i]);
    }
}

void print_hex(unsigned int val) {
    const char *hex = "0123456789abcdef";
    char buf[9];
    int i;
    for (i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    print("0x");
    print(buf);
}

void itoa(int n, char *out) {
    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    {
        char tmp[16];
        int i = 0;
        int neg = 0;
        int p = 0;
        if (n < 0) {
            neg = 1;
            n = -n;
        }
        while (n > 0 && i < 15) {
            tmp[i++] = (char)('0' + (n % 10));
            n /= 10;
        }
        if (neg) out[p++] = '-';
        while (i > 0) out[p++] = tmp[--i];
        out[p] = '\0';
    }
}

int parse_ip4(const char *s, unsigned int *out_be) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    int part = 0;
    unsigned int val = 0;
    int i;
    for (i = 0;; i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            val = val * 10 + (unsigned int)(ch - '0');
            if (val > 255) return -1;
        } else if (ch == '.' || ch == '\0' || ch == ' ') {
            if (part == 0) a = val;
            else if (part == 1) b = val;
            else if (part == 2) c = val;
            else if (part == 3) d = val;
            else return -1;
            part++;
            val = 0;
            if (ch == '\0' || ch == ' ') break;
        } else {
            return -1;
        }
    }
    if (part != 4) return -1;
    *out_be = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

void format_ip4(unsigned int ip_be, char *out) {
    int p = 0;
    int shift;
    for (shift = 24; shift >= 0; shift -= 8) {
        unsigned int octet = (ip_be >> shift) & 0xFF;
        if (octet >= 100) {
            out[p++] = '0' + (char)(octet / 100);
            octet %= 100;
            out[p++] = '0' + (char)(octet / 10);
            out[p++] = '0' + (char)(octet % 10);
        } else if (octet >= 10) {
            out[p++] = '0' + (char)(octet / 10);
            out[p++] = '0' + (char)(octet % 10);
        } else {
            out[p++] = '0' + (char)octet;
        }
        if (shift > 0) out[p++] = '.';
    }
    out[p] = '\0';
}

// SmallerC-generated code currently references $print directly.
void smallerc_print_alias(const char *s) __asm__("$print");
void smallerc_print_alias(const char *s) {
    print(s);
}
