#include "multiboot.h"
#include "lib.h"

static multiboot_info_t *multiboot_info = NULL;
static multiboot_module_t *initrd_module = NULL;

void multiboot_init(uint32_t magic, multiboot_info_t *mbi) {
  printf("Multiboot init...\n");

  // Verify magic number
  if (magic != MULTIBOOT_MAGIC) {
    printf("ERROR: Invalid multiboot magic: 0x%x (expected 0x%x)\n",
           magic, MULTIBOOT_MAGIC);
    return;
  }

  multiboot_info = mbi;
  printf("  Multiboot info at 0x%x\n", (uint32_t)mbi);
  printf("  Flags: 0x%x\n", mbi->flags);

  // Check memory info
  if (mbi->flags & MULTIBOOT_FLAG_MEM) {
    printf("  Memory: lower=%dKB upper=%dKB\n", mbi->mem_lower, mbi->mem_upper);
  }

  // Check for boot loader name
  if (mbi->flags & MULTIBOOT_FLAG_LOADER) {
    printf("  Bootloader: %s\n", (char *)mbi->boot_loader_name);
  }

  // Check for modules (initrd)
  if (mbi->flags & MULTIBOOT_FLAG_MODS) {
    printf("  Modules: count=%d addr=0x%x\n", mbi->mods_count, mbi->mods_addr);

    if (mbi->mods_count > 0) {
      multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;

      for (uint32_t i = 0; i < mbi->mods_count; i++) {
        printf("    Module %d: 0x%x - 0x%x (%d bytes)",
               i, mods[i].mod_start, mods[i].mod_end,
               mods[i].mod_end - mods[i].mod_start);

        if (mods[i].string) {
          printf(" '%s'", (char *)mods[i].string);
        }
        printf("\n");
      }

      // Save first module as initrd
      if (mbi->mods_count > 0) {
        initrd_module = &mods[0];
        printf("  Initrd: 0x%x - 0x%x (%d bytes)\n",
               initrd_module->mod_start,
               initrd_module->mod_end,
               initrd_module->mod_end - initrd_module->mod_start);
      }
    }
  } else {
    printf("  No modules loaded (no initrd)\n");
  }

  printf("Multiboot init complete\n");
}

multiboot_module_t *multiboot_get_initrd(void) {
  return initrd_module;
}
