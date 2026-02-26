#ifndef _MULTIBOOT_H
#define _MULTIBOOT_H

#include "lib.h"

// Multiboot magic number (passed in EAX)
#define MULTIBOOT_MAGIC 0x2BADB002

// Multiboot flags
#define MULTIBOOT_FLAG_MEM     0x001
#define MULTIBOOT_FLAG_DEVICE  0x002
#define MULTIBOOT_FLAG_CMDLINE 0x004
#define MULTIBOOT_FLAG_MODS    0x008
#define MULTIBOOT_FLAG_AOUT    0x010
#define MULTIBOOT_FLAG_ELF     0x020
#define MULTIBOOT_FLAG_MMAP    0x040
#define MULTIBOOT_FLAG_CONFIG  0x080
#define MULTIBOOT_FLAG_LOADER  0x100
#define MULTIBOOT_FLAG_APM     0x200
#define MULTIBOOT_FLAG_VBE     0x400

// Module structure
typedef struct {
  uint32_t mod_start;
  uint32_t mod_end;
  uint32_t string;    // C string (module name/cmdline)
  uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

// Multiboot info structure (passed by bootloader)
typedef struct {
  uint32_t flags;

  // Available memory from BIOS
  uint32_t mem_lower;  // KB of lower memory
  uint32_t mem_upper;  // KB of upper memory

  // Boot device
  uint32_t boot_device;

  // Kernel command line
  uint32_t cmdline;

  // Modules loaded
  uint32_t mods_count;
  uint32_t mods_addr;  // Address of first module

  // ELF section header table (if flags & MULTIBOOT_FLAG_ELF)
  uint32_t num;
  uint32_t size;
  uint32_t addr;
  uint32_t shndx;

  // Memory map
  uint32_t mmap_length;
  uint32_t mmap_addr;

  // Drive info
  uint32_t drives_length;
  uint32_t drives_addr;

  // ROM config table
  uint32_t config_table;

  // Boot loader name
  uint32_t boot_loader_name;

  // APM table
  uint32_t apm_table;

  // Video info
  uint32_t vbe_control_info;
  uint32_t vbe_mode_info;
  uint16_t vbe_mode;
  uint16_t vbe_interface_seg;
  uint16_t vbe_interface_off;
  uint16_t vbe_interface_len;
} __attribute__((packed)) multiboot_info_t;

// Parse multiboot info and initialize modules
void multiboot_init(uint32_t magic, multiboot_info_t *mbi);

// Get initrd module (first module)
multiboot_module_t *multiboot_get_initrd(void);

// VBE framebuffer info (set if bootloader provided VBE mode)
uint32_t multiboot_get_vbe_fb(void);      // Physical address (0 if unavailable)
uint32_t multiboot_get_vbe_width(void);
uint32_t multiboot_get_vbe_height(void);
uint32_t multiboot_get_vbe_pitch(void);   // Bytes per scanline
uint32_t multiboot_get_vbe_bpp(void);     // Bits per pixel
const char *multiboot_get_cmdline(void);  // Kernel command line or NULL

#endif
