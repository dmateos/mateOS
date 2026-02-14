#ifndef _UGFX_H
#define _UGFX_H

// Screen dimensions — set by ugfx_init() based on kernel gfx_info
extern int ugfx_width;
extern int ugfx_height;

// Initialize/exit graphics mode
int ugfx_init(void);
void ugfx_exit(void);

// Drawing primitives
void ugfx_pixel(int x, int y, unsigned char color);
unsigned char ugfx_read_pixel(int x, int y);
void ugfx_rect(int x, int y, int w, int h, unsigned char color);
void ugfx_rect_outline(int x, int y, int w, int h, unsigned char color);
void ugfx_clear(unsigned char color);
void ugfx_hline(int x, int y, int w, unsigned char color);
void ugfx_vline(int x, int y, int h, unsigned char color);

// Text rendering (8x8 font)
void ugfx_char(int x, int y, char c, unsigned char fg);
void ugfx_string(int x, int y, const char *str, unsigned char fg);
void ugfx_string_bg(int x, int y, const char *str, unsigned char fg, unsigned char bg);

// Input
unsigned char ugfx_getkey(void);    // Non-blocking, returns 0 if no key
unsigned char ugfx_waitkey(void);   // Blocking, polls with yield

// Buffer-mode drawing (for windowed apps drawing into pixel buffers)
void ugfx_buf_pixel(unsigned char *buf, int bw, int bh,
                    int x, int y, unsigned char color);
void ugfx_buf_rect(unsigned char *buf, int bw, int bh,
                   int x, int y, int w, int h, unsigned char color);
void ugfx_buf_clear(unsigned char *buf, int bw, int bh, unsigned char color);
void ugfx_buf_char(unsigned char *buf, int bw, int bh,
                   int x, int y, char c, unsigned char fg);
void ugfx_buf_string(unsigned char *buf, int bw, int bh,
                     int x, int y, const char *str, unsigned char fg);
void ugfx_buf_hline(unsigned char *buf, int bw, int bh,
                    int x, int y, int w, unsigned char color);

// Present an offscreen buffer to the active framebuffer.
void ugfx_present(const unsigned char *buf, int bw, int bh);

#endif
