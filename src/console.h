#ifndef _CONSOLE_H
#define _CONSOLE_H

#include "lib.h"

#define CONSOLE_LINE_MAX 256

void console_init(void);
void console_putchar(char c);
void console_handle_key(char c);
void console_execute_command(const char *line);

#endif
