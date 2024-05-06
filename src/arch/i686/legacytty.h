#ifndef _LEGACYTTY_H
#define _LEGACYTTY_H

#include "../../lib.h"

void init_term(void);
void terminal_setcolor(uint8_t color);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void term_putchar(char c);
void terminal_write(const char *data, size_t size);
void term_writestr(const char *data);

#endif