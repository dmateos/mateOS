#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>

// Mode 13h constants
#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_FB ((uint8_t *)0xC00A0000)

// VGA framebuffer physical address range (Mode 13h)
#define VGA_MODE13H_FB_START 0xA0000u
#define VGA_MODE13H_FB_END   0xB0000u

// VGA ports
#define VGA_MISC_WRITE 0x3C2
#define VGA_MISC_READ 0x3CC
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
#define VGA_AC_INDEX 0x3C0
#define VGA_AC_WRITE 0x3C0
#define VGA_AC_READ 0x3C1
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA 0x3C9
#define VGA_INSTAT_READ 0x3DA

// Bochs VGA (BGA) dispi registers — used by QEMU -vga std
#define VBE_DISPI_INDEX_PORT 0x01CE
#define VBE_DISPI_DATA_PORT 0x01CF
#define VBE_DISPI_INDEX_ID 0x0
#define VBE_DISPI_INDEX_XRES 0x1
#define VBE_DISPI_INDEX_YRES 0x2
#define VBE_DISPI_INDEX_BPP 0x3
#define VBE_DISPI_INDEX_ENABLE 0x4
#define VBE_DISPI_INDEX_BANK 0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH 0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET 0x8
#define VBE_DISPI_INDEX_Y_OFFSET 0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xA
#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_LFB_ENABLED 0x40

// Mode switching
void vga_enter_mode13h(void);
void vga_enter_text_mode(void);
int vga_is_mode13h(void);

// Bochs VGA high-res mode (returns LFB physical address, 0 on failure)
uint32_t vga_enter_bga_mode(int width, int height, int bpp);
void vga_exit_bga_mode(void);
int vga_bga_available(void);
int vga_is_graphics(void); // True if Mode 13h or BGA active

// Drawing primitives
void vga_put_pixel(int x, int y, uint8_t color);
void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
void vga_clear(uint8_t color);
void vga_draw_line(int x0, int y0, int x1, int y1, uint8_t color);

// Text rendering (8x8 bitmap font)
void vga_draw_char(int x, int y, char c, uint8_t color);
void vga_draw_string(int x, int y, const char *str, uint8_t color);

// Palette
void vga_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void vga_init_palette(void);

#endif
