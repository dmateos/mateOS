#include "keyboard.h"

const uint8_t kb_map[] = {
    0,   0,   '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
    'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
    'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' ',
};

uint8_t keyboard_translate(uint8_t scancode) {
  if (scancode & 0x80) {
    return 0;
  }

  char c = kb_map[scancode] & 0x7F;
  if (c) {
    return c;
  }

  return 0;
}

// Ring buffer for user-mode keyboard input
static uint8_t key_buffer[KEY_BUFFER_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;
static volatile int kb_enabled = 0;

void keyboard_buffer_init(void) {
  kb_head = 0;
  kb_tail = 0;
}

void keyboard_buffer_enable(int enable) {
  kb_enabled = enable;
  if (enable) {
    kb_head = 0;
    kb_tail = 0;
  }
}

int keyboard_buffer_is_enabled(void) {
  return kb_enabled;
}

int keyboard_buffer_push(uint8_t key) {
  if (!kb_enabled) return -1;
  int next = (kb_head + 1) % KEY_BUFFER_SIZE;
  if (next == kb_tail) return -1;  // Full
  key_buffer[kb_head] = key;
  kb_head = next;
  return 0;
}

uint8_t keyboard_buffer_pop(void) {
  if (kb_head == kb_tail) return 0;  // Empty
  uint8_t key = key_buffer[kb_tail];
  kb_tail = (kb_tail + 1) % KEY_BUFFER_SIZE;
  return key;
}

int keyboard_buffer_empty(void) {
  return kb_head == kb_tail;
}