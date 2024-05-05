#include "gdt.h"
#include "legacytty.h"

gdt_entry_t gdt[3];
gdt_ptr_t gp;

void init_gdt() {
  gp.limit = (sizeof(gdt_entry_t) * 3) - 1;
  gp.base = (uint32_t)&gdt;

  gdt[0].limit_low = 0xFFFF;
  gdt[0].base_low = 0x0000;
  gdt[0].base_middle = 0x00;
  gdt[0].access = 0x9A;
  gdt[0].granularity = 0xCF;
  gdt[0].base_high = 0x00;

  gdt[1].limit_low = 0xFFFF;
  gdt[1].base_low = 0x0000;
  gdt[1].base_middle = 0x00;
  gdt[1].access = 0x92;
  gdt[1].granularity = 0xCF;
  gdt[1].base_high = 0x00;

  gdt[2].limit_low = 0xFFFF;
  gdt[2].base_low = 0x0000;
  gdt[2].base_middle = 0x00;
  gdt[2].access = 0xFA;
  gdt[2].granularity = 0xCF;
  gdt[2].base_high = 0x00;

  terminal_writestring("GDT iniitalizing\n");
  asm volatile("lgdt %0" : : "m"(gp));
  terminal_writestring("GDT initialized\n");
}
