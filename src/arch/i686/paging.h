#ifndef _PAGING_H
#define _PAGING_H

#include "lib.h"

// Page table entry flags
#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4

typedef struct page_directory {
  uint32_t tables[1024];
} page_directory_t;

typedef struct page_table {
  uint32_t pages[1024];
} page_table_t;

typedef struct page_directory_entry {
  uint32_t present : 1;
  uint32_t rw : 1;
  uint32_t user : 1;
  uint32_t accessed : 1;
  uint32_t unused : 8;
  uint32_t frame : 20;
} page_directory_entry_t;

typedef struct page_table_entry {
  uint32_t present : 1;
  uint32_t rw : 1;
  uint32_t user : 1;
  uint32_t accessed : 1;
  uint32_t dirty : 1;
  uint32_t unused : 7;
  uint32_t frame : 20;
} page_table_entry_t;

// Boot-time paging setup: identity maps 0 to NUM_PAGE_TABLES*4MB
void init_paging(page_directory_t *page_dir, page_table_t *page_tables);
uint32_t get_cr2(void);
uint32_t get_cr3(void);

// Mark a page as user-accessible in the kernel page tables
void paging_set_user(uint32_t virtual_addr);

// Get current page tables for modification
page_table_t *paging_get_tables(void);

// Per-process address space management
page_directory_t *paging_create_address_space(void);
void paging_destroy_address_space(page_directory_t *page_dir);

// Map VBE framebuffer into kernel page directory (called once at gfx_init)
void paging_map_vbe(uint32_t phys_addr, uint32_t size);
int paging_map_page(page_directory_t *page_dir, uint32_t virtual_addr,
                    uint32_t physical_addr, uint32_t flags);
void paging_unmap_page(page_directory_t *page_dir, uint32_t virtual_addr);
void paging_switch(page_directory_t *page_dir);
page_directory_t *paging_get_kernel_dir(void);

#endif