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

// Scrollback buffer: circular buffer of 200 lines, each 80 uint16_t entries
#define SCROLLBACK_LINES 200
static uint16_t scrollback[SCROLLBACK_LINES][80];

// cursor_line: the scrollback line index where terminal_row's BOTTOM-MOST
// visible line lives. It's the index of the line at screen row (terminal_row).
// Range: 0..SCROLLBACK_LINES-1, wraps around.
static size_t cursor_line;        // scrollback index of current cursor line
static size_t total_lines;        // total lines ever used (capped at SCROLLBACK_LINES)
static int scroll_offset;         // 0 = live view, >0 = scrolled up N lines

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;

// Get scrollback index for a given screen row
static int scrollback_index_for_row(size_t row) {
  int idx = (int)cursor_line - (int)(terminal_row - row);
  while (idx < 0) idx += SCROLLBACK_LINES;
  return idx % SCROLLBACK_LINES;
}

// Clear a scrollback line
static void scrollback_clear_line(size_t idx) {
  uint16_t blank = vga_entry(' ', vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
  for (size_t x = 0; x < 80; x++) {
    scrollback[idx][x] = blank;
  }
}

// Redraw the VGA display from scrollback buffer
static void terminal_redraw(void) {
  if (vga_is_graphics()) return;

  for (size_t y = 0; y < TTY_HEIGHT; y++) {
    // lines_back from cursor_line: how many lines before the current cursor line
    // does this screen row represent?
    // Screen row terminal_row = cursor_line (0 lines back)
    // Screen row 0 = terminal_row lines back from cursor_line
    // With scroll_offset, we shift everything up by scroll_offset more lines
    int lines_back = (int)(terminal_row - y) + scroll_offset;

    if (lines_back < 0 || lines_back >= (int)total_lines) {
      // No data for this row - blank it
      for (size_t x = 0; x < TTY_WIDTH; x++) {
        terminal_buffer[y * TTY_WIDTH + x] = vga_entry(' ', vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
      }
    } else {
      int idx = (int)cursor_line - lines_back;
      while (idx < 0) idx += SCROLLBACK_LINES;
      idx = idx % SCROLLBACK_LINES;

      for (size_t x = 0; x < TTY_WIDTH; x++) {
        terminal_buffer[y * TTY_WIDTH + x] = scrollback[idx][x];
      }
    }
  }
}

void init_term(void) {
  terminal_row = 0;
  terminal_column = 0;
  terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
  terminal_buffer = (uint16_t *)0xB8000;
  cursor_line = 0;
  total_lines = 1;  // We start with line 0 allocated
  scroll_offset = 0;

  // Clear scrollback
  for (size_t i = 0; i < SCROLLBACK_LINES; i++) {
    scrollback_clear_line(i);
  }

  // Clear VGA display
  for (size_t y = 0; y < TTY_HEIGHT; y++) {
    for (size_t x = 0; x < TTY_WIDTH; x++) {
      terminal_buffer[y * TTY_WIDTH + x] = vga_entry(' ', terminal_color);
    }
  }

  // Initialize serial port for debug output
  serial_init();
}

void terminal_setcolor(uint8_t color) { terminal_color = color; }

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
  // Write to scrollback
  int idx = scrollback_index_for_row(y);
  scrollback[idx][x] = vga_entry(c, color);

  // Write to VGA if not scrolled up and not in graphics mode
  if (scroll_offset == 0 && !vga_is_graphics()) {
    terminal_buffer[y * TTY_WIDTH + x] = vga_entry(c, color);
  }
}

// Called when terminal_row advances to a new line (newline or line wrap)
static void terminal_newline(void) {
  terminal_column = 0;
  terminal_row++;

  if (terminal_row < TTY_HEIGHT) {
    // Still within visible area, just allocate a new scrollback line
    cursor_line = (cursor_line + 1) % SCROLLBACK_LINES;
    scrollback_clear_line(cursor_line);
    if (total_lines < SCROLLBACK_LINES) total_lines++;
  } else {
    // Screen needs to scroll
    terminal_row = TTY_HEIGHT - 1;
    cursor_line = (cursor_line + 1) % SCROLLBACK_LINES;
    scrollback_clear_line(cursor_line);
    if (total_lines < SCROLLBACK_LINES) total_lines++;

    if (scroll_offset == 0 && !vga_is_graphics()) {
      // Scroll VGA display up directly
      for (size_t y = 0; y < TTY_HEIGHT - 1; y++) {
        for (size_t x = 0; x < TTY_WIDTH; x++) {
          terminal_buffer[y * TTY_WIDTH + x] =
              terminal_buffer[(y + 1) * TTY_WIDTH + x];
        }
      }
      // Clear bottom line on VGA
      for (size_t x = 0; x < TTY_WIDTH; x++) {
        terminal_buffer[(TTY_HEIGHT - 1) * TTY_WIDTH + x] =
            vga_entry(' ', terminal_color);
      }
    }
  }
}

void term_putchar(char c) {
  // Always output to serial for debugging
  serial_putchar(c);

  // Skip VGA text buffer writes when in graphics mode
  if (vga_is_graphics()) return;

  // Snap to bottom on new output if scrolled
  if (scroll_offset != 0) {
    scroll_offset = 0;
    terminal_redraw();
  }

  if (c == '\n') {
    terminal_newline();
  } else if (c == '\b') {
    if (terminal_column > 0) {
      terminal_column--;
    }
  } else if (c == '\t') {
    size_t spaces = 4 - (terminal_column % 4);
    for (size_t i = 0; i < spaces; i++) {
      terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
      terminal_column++;
      if (terminal_column >= TTY_WIDTH) {
        terminal_newline();
      }
    }
  } else {
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    terminal_column++;
    if (terminal_column >= TTY_WIDTH) {
      terminal_newline();
    }
  }
}

void terminal_scroll_up(void) {
  if (vga_is_graphics()) return;
  int max_offset = (int)total_lines - (int)TTY_HEIGHT;
  if (max_offset < 0) max_offset = 0;

  scroll_offset += 5;
  if (scroll_offset > max_offset) scroll_offset = max_offset;
  terminal_redraw();
}

void terminal_scroll_down(void) {
  if (vga_is_graphics()) return;
  scroll_offset -= 5;
  if (scroll_offset < 0) scroll_offset = 0;
  terminal_redraw();
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
