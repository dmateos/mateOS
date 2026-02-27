#include "io.h"

void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void outw(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Initialize serial port COM1
void serial_init(void) {
    outb(SERIAL_COM1 + 1, 0x00); // Disable all interrupts
    outb(SERIAL_COM1 + 3, 0x80); // Enable DLAB (set baud rate divisor)
    outb(SERIAL_COM1 + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
    outb(SERIAL_COM1 + 1, 0x00); //                  (hi byte)
    outb(SERIAL_COM1 + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(SERIAL_COM1 + 2,
         0xC7); // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_COM1 + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

// Check if serial transmit buffer is empty
static int serial_is_transmit_empty(void) {
    return inb(SERIAL_COM1 + 5) & 0x20;
}

// Write a character to serial port
void serial_putchar(char c) {
    while (serial_is_transmit_empty() == 0)
        ;
    outb(SERIAL_COM1, c);
}