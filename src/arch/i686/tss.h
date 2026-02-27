#ifndef _TSS_H
#define _TSS_H

#include "lib.h"

// Task State Segment structure
typedef struct {
    uint32_t prev_tss; // Previous TSS link (unused in software task switching)
    uint32_t esp0;     // Stack pointer for ring 0 (kernel)
    uint32_t ss0;      // Stack segment for ring 0
    uint32_t esp1;     // Stack pointer for ring 1 (unused)
    uint32_t ss1;      // Stack segment for ring 1 (unused)
    uint32_t esp2;     // Stack pointer for ring 2 (unused)
    uint32_t ss2;      // Stack segment for ring 2 (unused)
    uint32_t cr3;      // Page directory base
    uint32_t eip;      // Instruction pointer
    uint32_t eflags;   // Flags register
    uint32_t eax;      // General purpose registers
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es; // Segment selectors
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;        // LDT selector
    uint16_t trap;       // Trap flag
    uint16_t iomap_base; // I/O map base address
} __attribute__((packed)) tss_entry_t;

// Segment selectors
#define KERNEL_CODE_SEG 0x08
#define KERNEL_DATA_SEG 0x10
#define USER_CODE_SEG 0x18
#define USER_DATA_SEG 0x20
#define TSS_SEG 0x28

// User mode selectors (with RPL=3)
#define USER_CODE_SEL (USER_CODE_SEG | 3) // 0x1B
#define USER_DATA_SEL (USER_DATA_SEG | 3) // 0x23

// Initialize the TSS
void tss_init(uint32_t kernel_stack);

// Set the kernel stack pointer in TSS (called on task switch)
void tss_set_kernel_stack(uint32_t esp0);

// Get the current kernel stack from TSS
uint32_t tss_get_kernel_stack(void);

// Assembly function to load TSS register
extern void flush_tss(uint16_t tss_selector);

#endif
