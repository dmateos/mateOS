/*
 * idt.h — 64-bit Interrupt Descriptor Table
 *
 * Each IDT entry is 16 bytes (two 8-byte words) in long mode.
 * We install 256 entries: 32 CPU exceptions, 16 hardware IRQs (after PIC remap),
 * and int 0x80 for syscalls.
 */
#ifndef _ARCH_X86_64_IDT_H
#define _ARCH_X86_64_IDT_H

#include "lib.h"

/* 64-bit IDT gate descriptor */
typedef struct {
    uint16_t offset_lo;   /* handler[15:0]  */
    uint16_t cs;          /* code segment selector */
    uint8_t  ist;         /* IST index (0 = legacy stack switching) */
    uint8_t  type_attr;   /* type/DPL/P flags */
    uint16_t offset_mid;  /* handler[31:16] */
    uint32_t offset_hi;   /* handler[63:32] */
    uint32_t reserved;
} __attribute__((packed)) idt_entry64_t;

void idt64_init(void);
void idt64_set_gate(uint8_t num, uintptr_t handler, uint8_t ist, uint8_t dpl);

#endif /* _ARCH_X86_64_IDT_H */
