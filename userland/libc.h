#ifndef _LIBC_H
#define _LIBC_H

// String functions
int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
void *memset(void *s, int c, unsigned int n);
void *memcpy(void *dst, const void *src, unsigned int n);
void *malloc(unsigned int n);
void *calloc(unsigned int n, unsigned int sz);
void *realloc(void *p, unsigned int n);
void free(void *p);

// I/O (stdout)
void print(const char *s);
void print_char(char c);
void print_num(int n);
void print_hex(unsigned int val);

// Integer to string (buffer-based, for window apps)
void itoa(int n, char *out);

// Network helpers
int parse_ip4(const char *s, unsigned int *out_be);
void format_ip4(unsigned int ip_be, char *out);

#endif
