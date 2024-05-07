#include "gdt.h"
#include "../../lib.h"

#define GDT_ENTRY_COUNT 3

gdt_entry_t gdt[GDT_ENTRY_COUNT];
gdt_ptr_t gp_ptr;

static void print_gdt(int segment, char *name) {
  printf("%s\n", name);
  printf("\tbase_low: 0x%x", gdt[segment].base_low);
  printf("\tbase_middle: 0x%x", gdt[segment].base_middle);
  printf("\tbase_high: 0x%x\n", gdt[segment].base_high);
  printf("\tlimit_low: 0x%x", gdt[segment].limit_low);
  printf("\taccess: 0x%x", gdt[segment].access);
  printf("\tgranularity: 0x%x\n", gdt[segment].granularity);
}

static void init_gdt_table() {
  // Flat memory model
  // Were not really going to use segments.
  gdt[0].limit_low = 0x0000;
  gdt[0].base_low = 0x0000;
  gdt[0].base_middle = 0x00;
  gdt[0].access = 0x00;
  gdt[0].granularity = 0x00;
  gdt[0].base_high = 0x00;

  // Kernel code segment
  gdt[1].limit_low = 0xFFFF;
  gdt[1].base_low = 0x0000;
  gdt[1].base_middle = 0x00;
  gdt[1].access = 0x9A;
  gdt[1].granularity = 0xCF;
  gdt[1].base_high = 0x00;

  // Kernel data segment
  gdt[2].limit_low = 0xFFFF;
  gdt[2].base_low = 0x0000;
  gdt[2].base_middle = 0x00;
  gdt[2].access = 0x92;
  gdt[2].granularity = 0xCF;
  gdt[2].base_high = 0x00;
}

void init_gdt() {
  printf("GDT initializing for i686\n");

  gp_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRY_COUNT) - 1;
  gp_ptr.base = (uint32_t)&gdt;

  init_gdt_table();

  print_gdt(0, "Null segment");
  print_gdt(1, "Kernel code segment");
  print_gdt(2, "Kernel data segment");

  // Load the GDT
  asm volatile("lgdt %0" : : "m"(gp_ptr));

  // Whatever you do with the GDT has no effect on the CPU until
  // you load new Segment Selectors into Segment Registers.
  asm volatile("mov $0x10, %ax\n");
  asm volatile("mov %ax, %ds\n");
  asm volatile("mov %ax, %es\n");
  asm volatile("mov %ax, %fs\n");
  asm volatile("mov %ax, %gs\n");
  asm volatile("mov %ax, %ss\n");
  asm volatile("ljmp $0x08, $next\n"
               "next:\n");

  printf("GDT initialized at address 0x%x\n", &gdt);
}
