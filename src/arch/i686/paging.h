#ifndef _PAGING_H
#define _PAGING_

#include "../../lib.h"

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

#endif