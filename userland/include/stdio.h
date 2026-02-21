#ifndef MATEOS_STDIO_H
#define MATEOS_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __mate_file FILE;
typedef struct {
    int off;
} fpos_t;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int putchar(int ch);
int fputc(int ch, FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int puts(const char *s);
int fputs(const char *s, FILE *stream);
int sprintf(char *dst, const char *fmt, ...);
int vsprintf(char *dst, const char *fmt, va_list ap);
int snprintf(char *dst, size_t cap, const char *fmt, ...);
int vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int fseek(FILE *stream, long off, int whence);
long ftell(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int remove(const char *path);

#endif
