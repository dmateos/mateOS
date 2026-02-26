#include "elf.h"
#include "lib.h"

int elf_validate(elf32_ehdr_t *hdr) {
  if (!hdr) return 0;

  // Check magic number
  if (hdr->e_ident[0] != 0x7F ||
      hdr->e_ident[1] != 'E' ||
      hdr->e_ident[2] != 'L' ||
      hdr->e_ident[3] != 'F') {
    return 0;
  }

  // Check 32-bit
  if (hdr->e_ident[4] != 1) {  // EI_CLASS = 1 for 32-bit
    printf("ELF: Not 32-bit\n");
    return 0;
  }

  // Check little-endian
  if (hdr->e_ident[5] != 1) {  // EI_DATA = 1 for little-endian
    printf("ELF: Not little-endian\n");
    return 0;
  }

  // Check version
  if (hdr->e_ident[6] != 1) {  // EI_VERSION = 1
    printf("ELF: Invalid version\n");
    return 0;
  }

  // Check machine type (i386)
  if (hdr->e_machine != EM_386) {
    printf("ELF: Not i386 (machine=%d)\n", hdr->e_machine);
    return 0;
  }

  // Check file type (executable)
  if (hdr->e_type != ET_EXEC) {
    printf("ELF: Not executable (type=%d)\n", hdr->e_type);
    return 0;
  }

  return 1;
}

void elf_print_info(elf32_ehdr_t *hdr) {
  if (!elf_validate(hdr)) {
    printf("Invalid ELF file\n");
    return;
  }

  printf("ELF32 Executable:\n");
  printf("  Entry: 0x%x\n", hdr->e_entry);
  printf("  Program headers: %d (offset=0x%x, size=%d)\n",
         hdr->e_phnum, hdr->e_phoff, hdr->e_phentsize);
  printf("  Section headers: %d (offset=0x%x, size=%d)\n",
         hdr->e_shnum, hdr->e_shoff, hdr->e_shentsize);

  // Print program headers
  elf32_phdr_t *phdr = (elf32_phdr_t *)((uint8_t *)hdr + hdr->e_phoff);
  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      printf("  LOAD: vaddr=0x%x memsz=%d filesz=%d flags=%c%c%c\n",
             phdr[i].p_vaddr,
             phdr[i].p_memsz,
             phdr[i].p_filesz,
             (phdr[i].p_flags & PF_R) ? 'R' : '-',
             (phdr[i].p_flags & PF_W) ? 'W' : '-',
             (phdr[i].p_flags & PF_X) ? 'X' : '-');
    }
  }
}
