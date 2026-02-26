#include "tss.h"
#include "gdt.h"
#include "lib.h"

// Global TSS entry
static tss_entry_t tss __attribute__((aligned(4096)));

// External function to set up TSS descriptor in GDT
extern void gdt_set_tss(uint32_t base, uint32_t limit);

void tss_init(uint32_t kernel_stack) {
  printf("TSS initializing...\n");

  // Clear the TSS
  memset(&tss, 0, sizeof(tss_entry_t));

  // Set up kernel stack for ring 0
  tss.ss0 = KERNEL_DATA_SEG;  // Kernel data segment
  tss.esp0 = kernel_stack;     // Kernel stack pointer

  // Set up segment selectors (kernel segments)
  tss.cs = KERNEL_CODE_SEG;
  tss.ss = KERNEL_DATA_SEG;
  tss.ds = KERNEL_DATA_SEG;
  tss.es = KERNEL_DATA_SEG;
  tss.fs = KERNEL_DATA_SEG;
  tss.gs = KERNEL_DATA_SEG;

  // I/O permission bitmap - set to size of TSS to disable I/O
  tss.iomap_base = sizeof(tss_entry_t);

  // Set up TSS descriptor in GDT
  gdt_set_tss((uint32_t)&tss, sizeof(tss_entry_t) - 1);

  // Load the TSS register
  flush_tss(TSS_SEG);

  printf("TSS initialized at 0x%x, kernel stack at 0x%x\n",
         (uint32_t)&tss, kernel_stack);
}

void tss_set_kernel_stack(uint32_t esp0) {
  tss.esp0 = esp0;
}

uint32_t tss_get_kernel_stack(void) {
  return tss.esp0;
}
