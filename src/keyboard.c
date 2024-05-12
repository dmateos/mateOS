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