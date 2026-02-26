#include "multiboot.h"
#include "lib.h"

static multiboot_info_t *multiboot_info = NULL;
static multiboot_module_t *initrd_module = NULL;
static const char *kernel_cmdline = NULL;

// VBE framebuffer info
static uint32_t vbe_fb_addr = 0;
static uint32_t vbe_fb_width = 0;
static uint32_t vbe_fb_height = 0;
static uint32_t vbe_fb_pitch = 0;
static uint32_t vbe_fb_bpp = 0;

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

  if (mbi->flags & MULTIBOOT_FLAG_CMDLINE) {
    kernel_cmdline = (const char *)(uint32_t)mbi->cmdline;
    if (kernel_cmdline) {
      printf("  Cmdline: %s\n", kernel_cmdline);
    }
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

  // Check for VBE info
  if (mbi->flags & MULTIBOOT_FLAG_VBE) {
    uint8_t *vbe = (uint8_t *)(uint32_t)mbi->vbe_mode_info;
    if (vbe) {
      vbe_fb_pitch  = *(uint16_t *)(vbe + 16);
      vbe_fb_width  = *(uint16_t *)(vbe + 18);
      vbe_fb_height = *(uint16_t *)(vbe + 20);
      vbe_fb_bpp    = *(uint8_t  *)(vbe + 25);
      vbe_fb_addr   = *(uint32_t *)(vbe + 40);

      printf("  VBE: %dx%dx%d pitch=%d fb=0x%x\n",
             vbe_fb_width, vbe_fb_height, vbe_fb_bpp,
             vbe_fb_pitch, vbe_fb_addr);
    }
  }

  printf("Multiboot init complete\n");
}

multiboot_module_t *multiboot_get_initrd(void) {
  return initrd_module;
}

uint32_t multiboot_get_vbe_fb(void)     { return vbe_fb_addr; }
uint32_t multiboot_get_vbe_width(void)  { return vbe_fb_width; }
uint32_t multiboot_get_vbe_height(void) { return vbe_fb_height; }
uint32_t multiboot_get_vbe_pitch(void)  { return vbe_fb_pitch; }
uint32_t multiboot_get_vbe_bpp(void)    { return vbe_fb_bpp; }
const char *multiboot_get_cmdline(void) { return kernel_cmdline; }
