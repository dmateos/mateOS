#ifndef _IO_H
#define _IO_H

#define outb(port, value)                                                      \
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port))

#define inb(port)                                                              \
  ({                                                                           \
    uint8_t _v;                                                                \
    __asm__ volatile("inb %1, %0" : "=a"(_v) : "Nd"(port));                    \
    _v;                                                                        \
  })

#endif