#include "timer.h"
#include "../../lib.h"
#include "io.h"
#include "interrupts.h"

#define MASTER_PIC_DATA 0x21

// System tick counter
static volatile uint32_t system_ticks = 0;
static uint32_t timer_frequency = 0;

void timer_handler(uint32_t irq __attribute__((unused)),
                   uint32_t error_code __attribute__((unused))) {
  system_ticks++;
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
