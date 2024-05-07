#include "idt.h"
#include "../../lib.h"

idt_entry_t idt_entries[256];
idt_ptr_t idt_ptr;

void write_idt_entry(uint8_t num, uint32_t base, uint16_t selector,
                     uint8_t flags) {
  idt_entries[num].base_low = base & 0xFFFF;
  idt_entries[num].base_high = (base >> 16) & 0xFFFF;
  idt_entries[num].selector = selector;
  idt_entries[num].zero = 0;
  idt_entries[num].flags = flags;
}

static void init_idt_table() {
  int segment = 0x08;
  // This is a dumb ass way to do this but i CBF writting a
  // lookup table in assembly.
  // is the segment offset correct here? i dont know.
  write_idt_entry(0, (uint32_t)isr0, segment, 0x8E);
  write_idt_entry(1, (uint32_t)isr1, segment, 0x8E);
  write_idt_entry(2, (uint32_t)isr2, segment, 0x8E);
  write_idt_entry(3, (uint32_t)isr3, segment, 0x8E);
  write_idt_entry(4, (uint32_t)isr4, segment, 0x8E);
  write_idt_entry(5, (uint32_t)isr5, segment, 0x8E);
  write_idt_entry(6, (uint32_t)isr6, segment, 0x8E);
  write_idt_entry(7, (uint32_t)isr7, segment, 0x8E);
  write_idt_entry(8, (uint32_t)isr8, segment, 0x8E);
  write_idt_entry(9, (uint32_t)isr9, segment, 0x8E);
  write_idt_entry(10, (uint32_t)isr10, segment, 0x8E);
  write_idt_entry(11, (uint32_t)isr11, segment, 0x8E);
  write_idt_entry(12, (uint32_t)isr12, segment, 0x8E);
  write_idt_entry(13, (uint32_t)isr13, segment, 0x8E);
  write_idt_entry(14, (uint32_t)isr14, segment, 0x8E);
  write_idt_entry(15, (uint32_t)isr15, segment, 0x8E);
  write_idt_entry(16, (uint32_t)isr16, segment, 0x8E);
  write_idt_entry(17, (uint32_t)isr17, segment, 0x8E);
  write_idt_entry(18, (uint32_t)isr18, segment, 0x8E);
  write_idt_entry(19, (uint32_t)isr19, segment, 0x8E);
  write_idt_entry(20, (uint32_t)isr20, segment, 0x8E);
  write_idt_entry(21, (uint32_t)isr21, segment, 0x8E);
  write_idt_entry(22, (uint32_t)isr22, segment, 0x8E);
  write_idt_entry(23, (uint32_t)isr23, segment, 0x8E);
  write_idt_entry(24, (uint32_t)isr24, segment, 0x8E);
  write_idt_entry(25, (uint32_t)isr25, segment, 0x8E);
  write_idt_entry(26, (uint32_t)isr26, segment, 0x8E);
  write_idt_entry(27, (uint32_t)isr27, segment, 0x8E);
  write_idt_entry(28, (uint32_t)isr28, segment, 0x8E);
  write_idt_entry(29, (uint32_t)isr29, segment, 0x8E);
  write_idt_entry(30, (uint32_t)isr30, segment, 0x8E);
  write_idt_entry(31, (uint32_t)isr31, segment, 0x8E);
}

void init_idt() {
  printf("IDT initializing\n");

  idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
  idt_ptr.base = (uint32_t)&idt_entries;

  init_idt_table();

  printf("IDT initialized with space for %d entries at address 0x%x\n",
         sizeof(idt_entries) / sizeof(idt_entry_t), &idt_entries);
  // Load the IDT
  asm volatile("lidt %0" : : "m"(idt_ptr));
  // Enable interrupts
  asm volatile("sti");
}

void idt_exception_handler(int number) {
  // printf("oh no! 0x%d\n", number);
  //  asm volatile("hlt");
}