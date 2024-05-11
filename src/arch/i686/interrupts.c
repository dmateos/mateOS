#include "interrupts.h"
#include "../../lib.h"
#include "io.h"
#include "util.h"

#define MASTER_PIC_COMMAND 0x20
#define MASTER_PIC_DATA 0x21
#define SLAVE_PIC_COMMAND 0xA0
#define SLAVE_PIC_DATA 0xA1

static void pic_remap(void) {
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

__attribute__((unused)) static void pic_disable(void) {
  outb(MASTER_PIC_DATA, 0xFF); // Mask all interrupts
  outb(SLAVE_PIC_DATA, 0xFF);  // Mask all interrupts
}

// make sure you pic_remap() before calling this
static void pic_only_keyboard(void) {
  outb(MASTER_PIC_DATA, 0xFD); // Mask all interrupts except IRQ1
  outb(SLAVE_PIC_DATA, 0xFF);  // Mask all interrupts
}

static void pic_acknowledge(int irq) {
  // Send both if its the slave
  if (irq >= 8) {
    outb(SLAVE_PIC_COMMAND, 0x20);
  }
  outb(MASTER_PIC_COMMAND, 0x20);
}

static void write_idt_entry(idt_entry_t *idt_entries, uint8_t num,
                            uint32_t base, uint16_t selector, uint8_t flags) {
  idt_entries[num].base_low = base & 0xFFFF;
  idt_entries[num].base_high = (base >> 16) & 0xFFFF;
  idt_entries[num].selector = selector;
  idt_entries[num].zero = 0;
  idt_entries[num].flags = flags;
}

static void init_idt_table(idt_entry_t *ide) {
  int segment = 0x08;
  // This is a dumb ass way to do this but i CBF writting a
  // lookup table in assembly.
  // is the segment offset correct here? i dont know.
  write_idt_entry(ide, 0, (uint32_t)isr0, segment, 0x8E);
  write_idt_entry(ide, 1, (uint32_t)isr1, segment, 0x8E);
  write_idt_entry(ide, 2, (uint32_t)isr2, segment, 0x8E);
  write_idt_entry(ide, 3, (uint32_t)isr3, segment, 0x8E);
  write_idt_entry(ide, 4, (uint32_t)isr4, segment, 0x8E);
  write_idt_entry(ide, 5, (uint32_t)isr5, segment, 0x8E);
  write_idt_entry(ide, 6, (uint32_t)isr6, segment, 0x8E);
  write_idt_entry(ide, 7, (uint32_t)isr7, segment, 0x8E);
  write_idt_entry(ide, 8, (uint32_t)isr8, segment, 0x8E);
  write_idt_entry(ide, 9, (uint32_t)isr9, segment, 0x8E);
  write_idt_entry(ide, 10, (uint32_t)isr10, segment, 0x8E);
  write_idt_entry(ide, 11, (uint32_t)isr11, segment, 0x8E);
  write_idt_entry(ide, 12, (uint32_t)isr12, segment, 0x8E);
  write_idt_entry(ide, 13, (uint32_t)isr13, segment, 0x8E);
  write_idt_entry(ide, 14, (uint32_t)isr14, segment, 0x8E);
  write_idt_entry(ide, 15, (uint32_t)isr15, segment, 0x8E);
  write_idt_entry(ide, 16, (uint32_t)isr16, segment, 0x8E);
  write_idt_entry(ide, 17, (uint32_t)isr17, segment, 0x8E);
  write_idt_entry(ide, 18, (uint32_t)isr18, segment, 0x8E);
  write_idt_entry(ide, 19, (uint32_t)isr19, segment, 0x8E);
  write_idt_entry(ide, 20, (uint32_t)isr20, segment, 0x8E);
  write_idt_entry(ide, 21, (uint32_t)isr21, segment, 0x8E);
  write_idt_entry(ide, 22, (uint32_t)isr22, segment, 0x8E);
  write_idt_entry(ide, 23, (uint32_t)isr23, segment, 0x8E);
  write_idt_entry(ide, 24, (uint32_t)isr24, segment, 0x8E);
  write_idt_entry(ide, 25, (uint32_t)isr25, segment, 0x8E);
  write_idt_entry(ide, 26, (uint32_t)isr26, segment, 0x8E);
  write_idt_entry(ide, 27, (uint32_t)isr27, segment, 0x8E);
  write_idt_entry(ide, 28, (uint32_t)isr28, segment, 0x8E);
  write_idt_entry(ide, 29, (uint32_t)isr29, segment, 0x8E);
  write_idt_entry(ide, 30, (uint32_t)isr30, segment, 0x8E);
  write_idt_entry(ide, 31, (uint32_t)isr31, segment, 0x8E);

  // IRQs
  write_idt_entry(ide, 32, (uint32_t)irq0, segment, 0x8E);
  write_idt_entry(ide, 33, (uint32_t)irq1, segment, 0x8E);
  write_idt_entry(ide, 34, (uint32_t)irq2, segment, 0x8E);
  write_idt_entry(ide, 35, (uint32_t)irq3, segment, 0x8E);
  write_idt_entry(ide, 36, (uint32_t)irq4, segment, 0x8E);
  write_idt_entry(ide, 37, (uint32_t)irq5, segment, 0x8E);
  write_idt_entry(ide, 38, (uint32_t)irq6, segment, 0x8E);
  write_idt_entry(ide, 39, (uint32_t)irq7, segment, 0x8E);
  write_idt_entry(ide, 40, (uint32_t)irq8, segment, 0x8E);
  write_idt_entry(ide, 41, (uint32_t)irq9, segment, 0x8E);
  write_idt_entry(ide, 42, (uint32_t)irq10, segment, 0x8E);
  write_idt_entry(ide, 43, (uint32_t)irq11, segment, 0x8E);
  write_idt_entry(ide, 44, (uint32_t)irq12, segment, 0x8E);
  write_idt_entry(ide, 45, (uint32_t)irq13, segment, 0x8E);
  write_idt_entry(ide, 46, (uint32_t)irq14, segment, 0x8E);
  write_idt_entry(ide, 47, (uint32_t)irq15, segment, 0x8E);
}

void init_idt(idt_ptr_t *idt_ptr, idt_entry_t *idt_entries) {
  printf("IDT initializing\n");

  idt_ptr->limit = sizeof(idt_entry_t) * 256 - 1;
  idt_ptr->base = (uint32_t)idt_entries;

  pic_remap();
  pic_only_keyboard();
  // pic_disable();
  init_idt_table(idt_entries);
  flush_idt(idt_ptr);

  printf("IDT initialized with space for %d entries at address 0x%x\n",
         sizeof(*idt_entries) / sizeof(idt_entry_t), idt_entries);
}

static unsigned int count = 0;
void idt_exception_handler(uint32_t number, uint32_t noerror) {
  if (number == 0xD) {
    // printf("oh no! 0x%x, noerror: %d, %d\n", number, noerror, count++);
  } else {
    printf("oh no! 0x%x, noerror: %d, %d\n", number, noerror, count++);
  }
}

void idt_irq_handler(uint32_t number, uint32_t number2) {
  printf("IRQ: 0x%d %d\n", number, number2);
  if (number == 0x1) {
    uint8_t scancode = inb(0x60); // read scancode from keyboard
    printf("Scancode: %c\n", scancode);
    pic_acknowledge(number);
  }
}