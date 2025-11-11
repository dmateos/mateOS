#include "paging.h"
#include "../../lib.h"

// Page directory and table flags
#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4

// Assembly functions to manipulate control registers
extern void enable_paging(uint32_t page_directory_physical);

void init_paging(page_directory_t *page_dir, page_table_t *page_table) {
  printf("Paging initialization starting\n");

  // Verify alignment - page directory and table must be 4KB aligned
  uint32_t pd_addr = (uint32_t)page_dir;
  uint32_t pt_addr = (uint32_t)page_table;

  if (pd_addr & 0xFFF) {
    printf("ERROR: Page directory not 4KB aligned (0x%x)\n", pd_addr);
    return;
  }
  if (pt_addr & 0xFFF) {
    printf("ERROR: Page table not 4KB aligned (0x%x)\n", pt_addr);
    return;
  }

  // Clear page directory
  memset(page_dir, 0, sizeof(page_directory_t));

  // Clear page table
  memset(page_table, 0, sizeof(page_table_t));

  // Identity map the first 4MB (0x0 to 0x400000)
  // This covers the kernel and any low memory we're using
  printf("Setting up identity mapping for first 4MB\n");

  for (uint32_t i = 0; i < 1024; i++) {
    // Each page is 4KB, so physical address = i * 0x1000
    uint32_t physical_addr = i * 0x1000;
    // Set present, writable, supervisor mode
    page_table->pages[i] = physical_addr | PAGE_PRESENT | PAGE_WRITE;
  }

  // Set up page directory entry 0 to point to our page table
  uint32_t page_table_physical = (uint32_t)page_table;
  page_dir->tables[0] = page_table_physical | PAGE_PRESENT | PAGE_WRITE;

  printf("Page directory at 0x%x\n", page_dir);
  printf("Page table at 0x%x\n", page_table);
  printf("Enabling paging...\n");

  // Enable paging by loading CR3 and setting CR0.PG
  enable_paging((uint32_t)page_dir);

  printf("Paging enabled successfully!\n");
  printf("CR3 = 0x%x\n", get_cr3());
}