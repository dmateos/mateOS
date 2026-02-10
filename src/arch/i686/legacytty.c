#include "legacytty.h"
#include "../../lib.h"
#include "io.h"
#include "vga.h"

static uint16_t *terminal_buffer = (uint16_t *)0xB8000;

/* Hardware text mode color constants. */
enum vga_color {
  VGA_COLOR_BLACK = 0,
  VGA_COLOR_BLUE = 1,
  VGA_COLOR_GREEN = 2,
  VGA_COLOR_CYAN = 3,
  VGA_COLOR_RED = 4,
  VGA_COLOR_MAGENTA = 5,
  VGA_COLOR_BROWN = 6,
  VGA_COLOR_LIGHT_GREY = 7,
  VGA_COLOR_DARK_GREY = 8,
  VGA_COLOR_LIGHT_BLUE = 9,
  VGA_COLOR_LIGHT_GREEN = 10,
  VGA_COLOR_LIGHT_CYAN = 11,
  VGA_COLOR_LIGHT_RED = 12,
  VGA_COLOR_LIGHT_MAGENTA = 13,
  VGA_COLOR_LIGHT_BROWN = 14,
  VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
  return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
  return (uint16_t)uc | (uint16_t)color << 8;
}

static const size_t TTY_WIDTH = 80;
static const size_t TTY_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;

void init_term(void) {
  terminal_row = 0;
  terminal_column = 0;
  terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
  terminal_buffer = (uint16_t *)0xB8000;
  for (size_t y = 0; y < TTY_HEIGHT; y++) {
    for (size_t x = 0; x < TTY_WIDTH; x++) {
      const size_t index = y * TTY_WIDTH + x;
      terminal_buffer[index] = vga_entry(' ', terminal_color);
    }
  }
  // Initialize serial port for debug output
  serial_init();
}

void terminal_setcolor(uint8_t color) { terminal_color = color; }

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
  const size_t index = y * TTY_WIDTH + x;
  terminal_buffer[index] = vga_entry(c, color);
}

void term_putchar(char c) {
  // Always output to serial for debugging
  serial_putchar(c);

  // Skip VGA text buffer writes when in graphics mode
  if (vga_is_graphics()) return;

  if (c == '\n') {
    // Newline - go to start of next line
    terminal_column = 0;
    terminal_row++;
    if (terminal_row >= TTY_HEIGHT) {
      terminal_row = TTY_HEIGHT - 1;
      // Scroll screen up
      for (size_t y = 0; y < TTY_HEIGHT - 1; y++) {
        for (size_t x = 0; x < TTY_WIDTH; x++) {
          terminal_buffer[y * TTY_WIDTH + x] =
              terminal_buffer[(y + 1) * TTY_WIDTH + x];
        }
      }
      // Clear bottom line
      for (size_t x = 0; x < TTY_WIDTH; x++) {
        terminal_buffer[(TTY_HEIGHT - 1) * TTY_WIDTH + x] =
            vga_entry(' ', terminal_color);
      }
    }
  } else if (c == '\b') {
    // Backspace - move cursor back (don't erase, just move)
    if (terminal_column > 0) {
      terminal_column--;
    }
  } else if (c == '\t') {
    // Tab - advance to next multiple of 4
    size_t spaces = 4 - (terminal_column % 4);
    for (size_t i = 0; i < spaces; i++) {
      terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
      terminal_column++;
      if (terminal_column >= TTY_WIDTH) {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row >= TTY_HEIGHT) {
          terminal_row = 0;
        }
      }
    }
  } else {
    // Normal character
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    terminal_column++;

    // Wrap to next line if needed
    if (terminal_column >= TTY_WIDTH) {
      terminal_column = 0;
      terminal_row++;
      if (terminal_row >= TTY_HEIGHT) {
        terminal_row = TTY_HEIGHT - 1;
        // Scroll screen up
        for (size_t y = 0; y < TTY_HEIGHT - 1; y++) {
          for (size_t x = 0; x < TTY_WIDTH; x++) {
            terminal_buffer[y * TTY_WIDTH + x] =
                terminal_buffer[(y + 1) * TTY_WIDTH + x];
          }
        }
        // Clear bottom line
        for (size_t x = 0; x < TTY_WIDTH; x++) {
          terminal_buffer[(TTY_HEIGHT - 1) * TTY_WIDTH + x] =
              vga_entry(' ', terminal_color);
        }
      }
    }
  }
}

void terminal_write(const char *data, size_t size) {
  for (size_t i = 0; i < size; i++)
    term_putchar(data[i]);
}

void term_writestr(const char *data) { terminal_write(data, strlen(data)); }

// Debug output to serial only
void serial_writestr(const char *data) {
  while (*data) {
    serial_putchar(*data);
    data++;
  }
}