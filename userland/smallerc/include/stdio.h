#ifndef _SMALLERC_STDIO_H
#define _SMALLERC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __smallerc_file FILE;

typedef struct {
    int off;
} fpos_t;

#define EOF (-1)

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int putchar(int ch);
int fputc(int ch, FILE *stream);
int fgetc(FILE *stream);
int puts(const char *s);
int fputs(const char *s, FILE *stream);
int sprintf(char *dst, const char *fmt, ...);
int vsprintf(char *dst, const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

#endif
