#include "paging.h"
#include "../../lib.h"

// Page directory and table flags
#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4

// Assembly functions to manipulate control registers
extern void enable_paging(uint32_t page_directory_physical);

// Number of page tables we use (each covers 4MB)
#define NUM_PAGE_TABLES 2

void init_paging(page_directory_t *page_dir, page_table_t *page_tables) {
  printf("Paging initialization starting\n");

  // Verify alignment - page directory must be 4KB aligned
  uint32_t pd_addr = (uint32_t)page_dir;
  if (pd_addr & 0xFFF) {
    printf("ERROR: Page directory not 4KB aligned (0x%x)\n", pd_addr);
    return;
  }

  // Clear page directory
  memset(page_dir, 0, sizeof(page_directory_t));

  // Set up page tables for identity mapping
  // Table 0: 0x000000 - 0x3FFFFF (first 4MB - kernel, VGA, etc)
  // Table 1: 0x400000 - 0x7FFFFF (second 4MB - heap region)
  for (uint32_t t = 0; t < NUM_PAGE_TABLES; t++) {
    page_table_t *pt = &page_tables[t];
    uint32_t pt_addr = (uint32_t)pt;

    if (pt_addr & 0xFFF) {
      printf("ERROR: Page table %d not 4KB aligned (0x%x)\n", t, pt_addr);
      return;
    }

    // Clear page table
    memset(pt, 0, sizeof(page_table_t));

    // Identity map this 4MB region
    for (uint32_t i = 0; i < 1024; i++) {
      // Physical address = table_number * 4MB + page_index * 4KB
      uint32_t physical_addr = (t * 0x400000) + (i * 0x1000);
      // Set present, writable, supervisor mode
      pt->pages[i] = physical_addr | PAGE_PRESENT | PAGE_WRITE;
    }

    // Set up page directory entry to point to this page table
    page_dir->tables[t] = pt_addr | PAGE_PRESENT | PAGE_WRITE;
  }

  printf("Identity mapped first %d MB\n", NUM_PAGE_TABLES * 4);
  printf("Page directory at 0x%x\n", page_dir);
  printf("Enabling paging...\n");

  // Enable paging by loading CR3 and setting CR0.PG
  enable_paging((uint32_t)page_dir);

  printf("Paging enabled successfully!\n");
  printf("CR3 = 0x%x\n", get_cr3());
}