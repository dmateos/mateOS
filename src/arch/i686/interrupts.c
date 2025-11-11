#include "interrupts.h"
#include "../../lib.h"
#include "io.h"
#include "util.h"

#define MASTER_PIC_COMMAND 0x20
#define MASTER_PIC_DATA 0x21
#define SLAVE_PIC_COMMAND 0xA0
#define SLAVE_PIC_DATA 0xA1

#define SEGMENT_OFFSET 0x08
#define PRIVILEGE 0x8E

// Interrupt Service Routine (ISR) handlers
void (*interruptPointers[256])(uint32_t, uint32_t) = {0};

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
  // This is a dumb ass way to do this but i CBF writting a
  // lookup table in assembly.
  write_idt_entry(ide, 0, (uint32_t)isr0, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 1, (uint32_t)isr1, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 2, (uint32_t)isr2, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 3, (uint32_t)isr3, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 4, (uint32_t)isr4, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 5, (uint32_t)isr5, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 6, (uint32_t)isr6, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 7, (uint32_t)isr7, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 8, (uint32_t)isr8, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 9, (uint32_t)isr9, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 10, (uint32_t)isr10, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 11, (uint32_t)isr11, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 12, (uint32_t)isr12, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 13, (uint32_t)isr13, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 14, (uint32_t)isr14, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 15, (uint32_t)isr15, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 16, (uint32_t)isr16, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 17, (uint32_t)isr17, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 18, (uint32_t)isr18, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 19, (uint32_t)isr19, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 20, (uint32_t)isr20, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 21, (uint32_t)isr21, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 22, (uint32_t)isr22, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 23, (uint32_t)isr23, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 24, (uint32_t)isr24, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 25, (uint32_t)isr25, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 26, (uint32_t)isr26, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 27, (uint32_t)isr27, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 28, (uint32_t)isr28, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 29, (uint32_t)isr29, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 30, (uint32_t)isr30, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 31, (uint32_t)isr31, SEGMENT_OFFSET, PRIVILEGE);

  // IRQs
  write_idt_entry(ide, 32, (uint32_t)irq0, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 33, (uint32_t)irq1, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 34, (uint32_t)irq2, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 35, (uint32_t)irq3, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 36, (uint32_t)irq4, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 37, (uint32_t)irq5, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 38, (uint32_t)irq6, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 39, (uint32_t)irq7, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 40, (uint32_t)irq8, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 41, (uint32_t)irq9, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 42, (uint32_t)irq10, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 43, (uint32_t)irq11, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 44, (uint32_t)irq12, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 45, (uint32_t)irq13, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 46, (uint32_t)irq14, SEGMENT_OFFSET, PRIVILEGE);
  write_idt_entry(ide, 47, (uint32_t)irq15, SEGMENT_OFFSET, PRIVILEGE);
}

void register_interrupt_handler(uint8_t n, void (*h)(uint32_t, uint32_t)) {
  interruptPointers[n] = h;
}

void init_idt(idt_ptr_t *idt_ptr, idt_entry_t *idt_entries) {
  printf("IDT initializing\n");

  idt_ptr->limit = sizeof(idt_entry_t) * 256 - 1;
  idt_ptr->base = (uint32_t)idt_entries;

  pic_remap();
  // Initially mask all interrupts - specific handlers will unmask as needed
  outb(MASTER_PIC_DATA, 0xFF);
  outb(SLAVE_PIC_DATA, 0xFF);

  init_idt_table(idt_entries);
  flush_idt(idt_ptr);

  printf("IDT initialized with space for %d entries at address 0x%x\n", 256,
         idt_entries);
}

void idt_breakpoint(void) { asm volatile("int $0x03"); }

void idt_exception_handler(uint32_t number, uint32_t noerror) {
  switch (number) {
  case 0x0:
    printf("Divide by zero\n");
    break;
  case 0x6:
    printf("Invalid opcode\n");
    break;
  case 0x8:
    printf("Double fault\n");
    break;
  case 0xD:
    printf("General protection fault\n");
    break;
  case 0xE: {
    extern uint32_t get_cr2(void);
    uint32_t fault_addr = get_cr2();
    printf("Page fault at address 0x%x\n", fault_addr);
    printf("Error code: 0x%x (", noerror);
    if (noerror & 0x1) printf("present "); else printf("not-present ");
    if (noerror & 0x2) printf("write "); else printf("read ");
    if (noerror & 0x4) printf("user"); else printf("supervisor");
    printf(")\n");
    break;
  }
  case 0x03:
    printf("Breakpoint\n");
    break;
  default:
    printf("Exception: 0x%x, %d\n", number, noerror);
  }
}

void idt_irq_handler(uint32_t number, uint32_t number2) {
  // printf("IRQ: 0x%x (%d) 0x%x (%d)\n", number, number, number2, number2);

  // Check for spurious IRQs on IRQ7 and IRQ15
  if (number == 0x27) {  // IRQ7
    uint8_t isr = inb(MASTER_PIC_COMMAND);
    if (!(isr & 0x80)) {
      return;  // Spurious IRQ7, don't acknowledge
    }
  } else if (number == 0x2F) {  // IRQ15
    uint8_t isr = inb(SLAVE_PIC_COMMAND);
    if (!(isr & 0x80)) {
      outb(MASTER_PIC_COMMAND, 0x20);  // Acknowledge master only
      return;
    }
  }

  switch (number) {
  case 0x21:
    break;
  case 0x20:
    break;
  default:
    printf("Unknown IRQ 0x%x 0x%x\n", number, number2);
  }

  // Convert interrupt vector to IRQ number (0-15)
  uint8_t irq = number - 0x20;
  pic_acknowledge(irq);

  if (interruptPointers[number] != 0) {
    interruptPointers[number](number, number2);
  }
}