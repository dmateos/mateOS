#include "keyboard.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/legacytty.h"
#include "console.h"
#include "utils/kring.h"

static int kb_extended = 0;

static const uint8_t kb_map[] = {
    0,   0,   '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
    'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
    'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' ',
};

static const uint8_t kb_map_shift[] = {
    0,   0,   '!',  '@',  '#',  '$', '%', '^',  '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'Q',  'W', 'E', 'R',  'T', 'Y', 'U', 'I',
    'O', 'P', '{',  '}',  '\n', 0,   'A', 'S',  'D', 'F', 'G', 'H',
    'J', 'K', 'L',  ':',  '"',  '~', 0,   '|',  'Z', 'X', 'C', 'V',
    'B', 'N', 'M',  '<',  '>',  '?', 0,   '*',  0,   ' ',
};

#define LSHIFT_SCAN 0x2A
#define RSHIFT_SCAN 0x36
#define LCTRL_SCAN  0x1D

static volatile int shift_held = 0;
static volatile int ctrl_held = 0;

uint8_t keyboard_translate(uint8_t scancode) {
  // Track modifier key press/release
  if (scancode == LSHIFT_SCAN || scancode == RSHIFT_SCAN) {
    shift_held = 1;
    return 0;
  }
  if (scancode == (LSHIFT_SCAN | 0x80) || scancode == (RSHIFT_SCAN | 0x80)) {
    shift_held = 0;
    return 0;
  }
  if (scancode == LCTRL_SCAN) {
    ctrl_held = 1;
    return 0;
  }
  if (scancode == (LCTRL_SCAN | 0x80)) {
    ctrl_held = 0;
    return 0;
  }

  if (scancode & 0x80) {
    return 0;
  }

  if (scancode >= sizeof(kb_map)) return 0;

  const uint8_t *map = shift_held ? kb_map_shift : kb_map;
  char c = map[scancode] & 0x7F;
  if (!c) return 0;

  // Ctrl+letter produces ASCII 1-26 (Ctrl+A=1, Ctrl+S=19, etc.)
  if (ctrl_held && c >= 'a' && c <= 'z') {
    return (uint8_t)(c - 'a' + 1);
  }

  return c;
}

// Ring buffer for user-mode keyboard input
static uint8_t key_buffer_storage[KEY_BUFFER_SIZE];
static kring_u8_t key_buffer;
static volatile int kb_enabled = 0;

void keyboard_buffer_init(void) {
  kring_u8_init(&key_buffer, key_buffer_storage, KEY_BUFFER_SIZE);
}

void keyboard_buffer_enable(int enable) {
  kb_enabled = enable;
  if (enable) {
    kring_u8_reset(&key_buffer);
  }
}

int keyboard_buffer_is_enabled(void) {
  return kb_enabled;
}

int keyboard_buffer_push(uint8_t key) {
  if (!kb_enabled) return -1;
  return kring_u8_push(&key_buffer, key);
}

uint8_t keyboard_buffer_pop(void) {
  uint8_t key = 0;
  if (kring_u8_pop(&key_buffer, &key) < 0) return 0;
  return key;
}

int keyboard_buffer_empty(void) {
  return kring_u8_empty(&key_buffer);
}

static void keyboard_irq_handler(uint32_t number __attribute__((unused)),
                                 uint32_t error_code __attribute__((unused))) {
  uint8_t scancode = inb(IO_KB_DATA);

  if (scancode == 0xE0) {
    kb_extended = 1;
    return;
  }

  if (kb_extended) {
    kb_extended = 0;
    if (!(scancode & 0x80)) {
      if (scancode == 0x49) {
        terminal_scroll_up();
        return;
      } else if (scancode == 0x51) {
        terminal_scroll_down();
        return;
      } else if (keyboard_buffer_is_enabled()) {
        uint8_t key = 0;
        if (scancode == 0x4B) key = KEY_LEFT;
        else if (scancode == 0x4D) key = KEY_RIGHT;
        else if (scancode == 0x48) key = KEY_UP;
        else if (scancode == 0x50) key = KEY_DOWN;
        if (key) {
          keyboard_buffer_push(key);
          return;
        }
      }
    }
    return;
  }

  if (keyboard_buffer_is_enabled()) {
    char c = keyboard_translate(scancode);
    if (c) keyboard_buffer_push((uint8_t)c);
    return;
  }

  char c = keyboard_translate(scancode);
  if (c) console_handle_key(c);
}

void keyboard_init_interrupts(void) {
  register_interrupt_handler(0x21, keyboard_irq_handler);
}
