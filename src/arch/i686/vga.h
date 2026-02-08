#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>

// Mode 13h constants
#define VGA_WIDTH  320
#define VGA_HEIGHT 200
#define VGA_FB     ((uint8_t *)0xA0000)

// VGA ports
#define VGA_MISC_WRITE      0x3C2
#define VGA_MISC_READ       0x3CC
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_AC_INDEX        0x3C0
#define VGA_AC_WRITE        0x3C0
#define VGA_AC_READ         0x3C1
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA        0x3C9
#define VGA_INSTAT_READ     0x3DA

// Mode switching
void vga_enter_mode13h(void);
void vga_enter_text_mode(void);
int  vga_is_mode13h(void);

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
