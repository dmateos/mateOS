#include "686init.h"

#include "gdt.h"
#include "interrupts.h"
#include "legacytty.h"
#include "paging.h"
#include "timer.h"
#include "util.h"

static gdt_entry_t gdt[3] = {0};
static idt_entry_t idt_entries[256] = {0};

static gdt_ptr_t gp_ptr = {0};
static idt_ptr_t idt_ptr = {0};

// Page directory and tables must be 4KB (0x1000) aligned
// We need 2 page tables: one for 0-4MB, one for 4MB-8MB (heap)
static page_directory_t page_dir __attribute__((aligned(4096))) = {0};
static page_table_t page_tables[2] __attribute__((aligned(4096))) = {0};

void init_686(void) {
  init_term();

  printf("mateOS kernel started\n");
  if (!check_protected_mode()) {
    printf("Protected mode not enabled\n");
    return;
  }

  // Global and Interrupt Descriptor Tables
  init_gdt(&gp_ptr, gdt);
  init_idt(&idt_ptr, idt_entries);

  // Initialize paging with identity mapping (0-8MB)
  init_paging(&page_dir, page_tables);

  // Initialize system timer (100 Hz)
  init_timer(100);

  printf("mateOS init done\n");
}