#ifndef _LIBC_H
#define _LIBC_H

#include <stddef.h>

// String functions
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strerror(int errnum);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);

// I/O (stdout)
void print(const char *s);
void print_char(char c);
void print_num(int n);
void print_hex(unsigned int val);

// Integer to string (buffer-based, for window apps)
void itoa(int n, char *out);

int atoi(const char *s);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

// Network helpers
int parse_ip4(const char *s, unsigned int *out_be);
void format_ip4(unsigned int ip_be, char *out);

#endif
