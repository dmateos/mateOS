#ifndef _ELF_H
#define _ELF_H

#include "lib.h"

// ELF32 format structures

#define ELF_MAGIC 0x464C457F  // 0x7F 'E' 'L' 'F'

// ELF file types
#define ET_NONE   0  // No file type
#define ET_REL    1  // Relocatable
#define ET_EXEC   2  // Executable
#define ET_DYN    3  // Shared object
#define ET_CORE   4  // Core file

// ELF machine types
#define EM_386    3  // Intel 80386

// Program header types
#define PT_NULL    0  // Unused
#define PT_LOAD    1  // Loadable segment
#define PT_DYNAMIC 2  // Dynamic linking info
#define PT_INTERP  3  // Interpreter path
#define PT_NOTE    4  // Auxiliary info
#define PT_SHLIB   5  // Reserved
#define PT_PHDR    6  // Program header table

// Program header flags
#define PF_X  0x1  // Execute
#define PF_W  0x2  // Write
#define PF_R  0x4  // Read

// ELF32 Header
typedef struct {
  uint8_t  e_ident[16];   // Magic number and other info
  uint16_t e_type;        // Object file type
  uint16_t e_machine;     // Architecture
  uint32_t e_version;     // Object file version
  uint32_t e_entry;       // Entry point virtual address
  uint32_t e_phoff;       // Program header table file offset
  uint32_t e_shoff;       // Section header table file offset
  uint32_t e_flags;       // Processor-specific flags
  uint16_t e_ehsize;      // ELF header size in bytes
  uint16_t e_phentsize;   // Program header table entry size
  uint16_t e_phnum;       // Program header table entry count
  uint16_t e_shentsize;   // Section header table entry size
  uint16_t e_shnum;       // Section header table entry count
  uint16_t e_shstrndx;    // Section header string table index
} __attribute__((packed)) elf32_ehdr_t;

// ELF32 Program Header
typedef struct {
  uint32_t p_type;    // Segment type
  uint32_t p_offset;  // Segment file offset
  uint32_t p_vaddr;   // Segment virtual address
  uint32_t p_paddr;   // Segment physical address
  uint32_t p_filesz;  // Segment size in file
  uint32_t p_memsz;   // Segment size in memory
  uint32_t p_flags;   // Segment flags
  uint32_t p_align;   // Segment alignment
} __attribute__((packed)) elf32_phdr_t;

// Validate ELF header
int elf_validate(elf32_ehdr_t *hdr);

// Print ELF info for debugging
void elf_print_info(elf32_ehdr_t *hdr);

#endif
