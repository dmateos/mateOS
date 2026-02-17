#include "libc.h"
#include "syscalls.h"

typedef struct {
    unsigned int size;
} alloc_hdr_t;

// ---- String functions ----

int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

void *memset(void *s, int c, unsigned int n) {
    unsigned char *p = (unsigned char *)s;
    for (unsigned int i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, unsigned int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (unsigned int i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

// ---- Heap allocation (simple sbrk-backed allocator) ----

static unsigned int align8(unsigned int n) {
    return (n + 7u) & ~7u;
}

void *malloc(unsigned int n) {
    if (n == 0) return 0;
    unsigned int need = align8(n + (unsigned int)sizeof(alloc_hdr_t));
    alloc_hdr_t *h = (alloc_hdr_t *)sbrk((int)need);
    if ((unsigned int)h == 0xFFFFFFFFu) return 0;
    h->size = n;
    return (void *)(h + 1);
}

void *calloc(unsigned int n, unsigned int sz) {
    unsigned int total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *p) {
    (void)p;
    // No-op for now. This is sufficient for early compiler bring-up.
}

void *realloc(void *p, unsigned int n) {
    if (!p) return malloc(n);
    if (n == 0) return 0;
    alloc_hdr_t *h = ((alloc_hdr_t *)p) - 1;
    void *np = malloc(n);
    if (!np) return 0;
    unsigned int copy = h->size < n ? h->size : n;
    memcpy(np, p, copy);
    return np;
}

// ---- I/O functions ----

void print(const char *s) {
    write(1, s, strlen(s));
}

void print_char(char c) {
    write(1, &c, 1);
}

void print_num(int n) {
    if (n < 0) { print_char('-'); n = -n; }
    if (n == 0) { print_char('0'); return; }
    char buf[12];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) print_char(buf[--i]);
}

void print_hex(unsigned int val) {
    const char *hex = "0123456789abcdef";
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    print("0x");
    print(buf);
}

// ---- Integer to string (buffer-based) ----

void itoa(int n, char *out) {
    if (n == 0) { out[0] = '0'; out[1] = '\0'; return; }
    char tmp[16];
    int i = 0, neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    while (n > 0 && i < 15) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    int p = 0;
    if (neg) out[p++] = '-';
    while (i > 0) out[p++] = tmp[--i];
    out[p] = '\0';
}

// ---- Network helpers ----

int parse_ip4(const char *s, unsigned int *out_be) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    int part = 0;
    unsigned int val = 0;
    for (int i = 0; ; i++) {
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
    for (int shift = 24; shift >= 0; shift -= 8) {
        unsigned int octet = (ip_be >> shift) & 0xFF;
        if (octet >= 100) { out[p++] = '0' + (char)(octet / 100); octet %= 100; out[p++] = '0' + (char)(octet / 10); out[p++] = '0' + (char)(octet % 10); }
        else if (octet >= 10) { out[p++] = '0' + (char)(octet / 10); out[p++] = '0' + (char)(octet % 10); }
        else { out[p++] = '0' + (char)octet; }
        if (shift > 0) out[p++] = '.';
    }
    out[p] = '\0';
}
