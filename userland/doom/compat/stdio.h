#ifndef _DOOM_STDIO_H
#define _DOOM_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct FILE {
    int fd;
    long pos;
    int eof;
    int err;
    int mode;
    int is_stdio;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fflush(FILE *f);

int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);

int fgetc(FILE *f);
int fputc(int c, FILE *f);

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);

int puts(const char *s);
int putchar(int c);
int fileno(FILE *f);
int isatty(int fd);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);

int sscanf(const char *str, const char *fmt, ...);

#endif
