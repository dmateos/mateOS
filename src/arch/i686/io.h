#ifndef _IO_H
#define _IO_H

#include "../../lib.h"
#include <stdint.h>

#define IO_KB_DATA 0x60

// Serial port (COM1)
#define SERIAL_COM1 0x3F8

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);

// Serial port functions
void serial_init(void);
void serial_putchar(char c);

#endif