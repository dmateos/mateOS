#include "idt.h"
#include "../../lib.h"
#include "io.h"
#include "util.h"

idt_entry_t idt_entries[256];
idt_ptr_t idt_ptr;

#define MASTER_PIC_COMMAND 0x20
#define MASTER_PIC_DATA 0x21
#define SLAVE_PIC_COMMAND 0xA0
#define SLAVE_PIC_DATA 0xA1

static void pic_remap() {
  // Remap the PIC so we can use interrupts
  outb(MASTER_PIC_COMMAND, 0x11); // Start initialization sequence
  outb(SLAVE_PIC_COMMAND, 0x11);  // Start initialization sequence
  outb(MASTER_PIC_DATA, 0x20);    // Set master offset to 0x20
  outb(SLAVE_PIC_DATA, 0x28);     // Set slave offset to 0x28
  outb(MASTER_PIC_DATA, 0x04);    // Tell master there is a slave at IRQ2
  outb(SLAVE_PIC_DATA, 0x02);     // Tell slave its cascade identity
  outb(MASTER_PIC_DATA, 0x01);    // 8086 mode
  outb(SLAVE_PIC_DATA, 0x01);     // 8086 mode
  outb(MASTER_PIC_DATA, 0x00);    // Mask all interrupts
  outb(SLAVE_PIC_DATA, 0x00);     // Mask all interrupts
}

static void pic_disable() {
  outb(MASTER_PIC_DATA, 0xFF); // Mask all interrupts
  outb(SLAVE_PIC_DATA, 0xFF);  // Mask all interrupts
}

static void pic_only_keyboard() {
  outb(MASTER_PIC_DATA, 0xFD); // Mask all interrupts except IRQ1
  outb(SLAVE_PIC_DATA, 0xFF);  // Mask all interrupts
}

static void pic_acknowledge(int irq) {
  if (irq >= 8) {
    outb(SLAVE_PIC_COMMAND, 0x20);
  }
  outb(MASTER_PIC_COMMAND, 0x20);
}

static void write_idt_entry(uint8_t num, uint32_t base, uint16_t selector,
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

  // IRQs
  write_idt_entry(32, (uint32_t)irq0, segment, 0x8E);
  write_idt_entry(33, (uint32_t)irq1, segment, 0x8E);
  write_idt_entry(34, (uint32_t)irq2, segment, 0x8E);
  write_idt_entry(35, (uint32_t)irq3, segment, 0x8E);
  write_idt_entry(36, (uint32_t)irq4, segment, 0x8E);
  write_idt_entry(37, (uint32_t)irq5, segment, 0x8E);
  write_idt_entry(38, (uint32_t)irq6, segment, 0x8E);
  write_idt_entry(39, (uint32_t)irq7, segment, 0x8E);
  write_idt_entry(40, (uint32_t)irq8, segment, 0x8E);
  write_idt_entry(41, (uint32_t)irq9, segment, 0x8E);
  write_idt_entry(42, (uint32_t)irq10, segment, 0x8E);
  write_idt_entry(43, (uint32_t)irq11, segment, 0x8E);
  write_idt_entry(44, (uint32_t)irq12, segment, 0x8E);
  write_idt_entry(45, (uint32_t)irq13, segment, 0x8E);
  write_idt_entry(46, (uint32_t)irq14, segment, 0x8E);
  write_idt_entry(47, (uint32_t)irq15, segment, 0x8E);
}

void init_idt() {
  printf("IDT initializing\n");

  idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
  idt_ptr.base = (uint32_t)&idt_entries;

  // pic_remap();
  //  pic_disable();
  pic_only_keyboard();
  init_idt_table();
  flush_idt(&idt_ptr);

  printf("IDT initialized with space for %d entries at address 0x%x\n",
         sizeof(idt_entries) / sizeof(idt_entry_t), &idt_entries);
}

void idt_exception_handler(int number, int noerror) {
  printf("oh no! 0x%d, noerror: %d\n", number, noerror);
  //    pic_acknowledge(number);
  // halt_and_catch_fire();
}

void idt_irq_handler(int number, int number2) {
  printf("IRQ: 0x%d %d\n", number, number2);
  // pic_acknowledge(number);
  // halt_and_catch_fire();
}