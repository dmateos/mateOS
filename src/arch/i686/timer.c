#include "timer.h"
#include "../../lib.h"
#include "../../task.h"
#include "io.h"
#include "interrupts.h"

#define MASTER_PIC_COMMAND 0x20
#define MASTER_PIC_DATA 0x21

// System tick counter
static volatile uint32_t system_ticks = 0;
static uint32_t timer_frequency = 0;

// Original timer handler (for non-multitasking mode)
void timer_handler(uint32_t irq __attribute__((unused)),
                   uint32_t error_code __attribute__((unused))) {
  system_ticks++;
}

// Timer handler with context switch support
// Called from assembly, returns new ESP
// is_hw: 1 if called from a real hardware IRQ, 0 if from software int $0x81
uint32_t *timer_handler_switch(uint32_t *esp, uint32_t is_hw) {
  if (is_hw) {
    system_ticks++;
    // Only send EOI for real hardware interrupts
    outb(MASTER_PIC_COMMAND, 0x20);
  }

  // Call scheduler if multitasking is enabled
  if (task_is_enabled()) {
    return schedule(esp);
  }

  return esp;
}

void init_timer(uint32_t frequency) {
  printf("Timer initializing at %d Hz\n", frequency);

  timer_frequency = frequency;

  // Register timer interrupt handler (IRQ0 = interrupt 0x20)
  register_interrupt_handler(0x20, timer_handler);

  // Configure PIT channel 0 (system timer)
  // Command: channel 0, access mode lobyte/hibyte, mode 3 (square wave), binary
  uint32_t divisor = 1193180 / frequency;

  outb(0x43, 0x36);  // Command register
  outb(0x40, divisor & 0xFF);  // Low byte
  outb(0x40, (divisor >> 8) & 0xFF);  // High byte

  // Unmask IRQ0 (timer) and IRQ1 (keyboard)
  // Bit 0 = IRQ0 (timer), Bit 1 = IRQ1 (keyboard)
  // 0xFC = 11111100 = mask all except IRQ0 and IRQ1
  outb(MASTER_PIC_DATA, 0xFC);

  printf("Timer initialized - divisor: %d, ticks per second: %d\n",
         divisor, frequency);
}

uint32_t get_tick_count(void) {
  return system_ticks;
}

uint32_t get_uptime_seconds(void) {
  if (timer_frequency == 0) {
    return 0;
  }
  return system_ticks / timer_frequency;
}
