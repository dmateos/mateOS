#ifndef MATEOS_STDLIB_H
#define MATEOS_STDLIB_H

#include <stddef.h>

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

void exit(int code);
int atoi(const char *s);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
char *getenv(const char *name);
char *realpath(const char *path, char *resolved_path);
extern char **environ;

#endif
