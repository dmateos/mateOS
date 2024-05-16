#include "686init.h"

#include "gdt.h"
#include "interrupts.h"
#include "legacytty.h"
#include "paging.h"
#include "util.h"

static gdt_entry_t gdt[3] = {0};
static idt_entry_t idt_entries[256] = {0};

static gdt_ptr_t gp_ptr = {0};
static idt_ptr_t idt_ptr = {0};

static page_directory_t page_dir = {0};
static page_table_t page_table = {0};

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

  printf("mateOS init done\n");
}