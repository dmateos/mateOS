#include "gdt.h"
#include "legacytty.h"

gdt_entry_t gdt[3];
gdt_ptr_t gp;

void init_gdt() {
  gp.limit = (sizeof(gdt_entry_t) * 3) - 1;
  gp.base = (uint32_t)&gdt;

  // Flat memory model
  // Were not really going to use segments.

  // Kernel code segment
  gdt[0].limit_low = 0xFFFF;
  gdt[0].base_low = 0x0000;
  gdt[0].base_middle = 0x00;
  gdt[0].access = 0x9A;
  gdt[0].granularity = 0xCF;
  gdt[0].base_high = 0x00;

  // Kernel data segment
  gdt[1].limit_low = 0xFFFF;
  gdt[1].base_low = 0x0000;
  gdt[1].base_middle = 0x00;
  gdt[1].access = 0x92;
  gdt[1].granularity = 0xCF;
  gdt[1].base_high = 0x00;

  term_writestr("GDT initializing\n");
  asm volatile("lgdt %0" : : "m"(gp));

  // Whatever you do with the GDT has no effect on the CPU until
  // you load new Segment Selectors into Segment Registers.
  asm volatile("mov $0x00, %ax\n");
  asm volatile("mov %ax, %ds\n");
  asm volatile("mov %ax, %es\n");
  asm volatile("mov %ax, %fs\n");
  asm volatile("mov %ax, %gs\n");
  // asm volatile("mov %ax, %ss\n");
  // asm volatile("ljmp $0x00, $next\n next:");

  term_writestr("GDT initialized\n");
}
