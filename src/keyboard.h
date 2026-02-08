#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "lib.h"

#define KEY_BUFFER_SIZE 32

uint8_t keyboard_translate(uint8_t scancode);

// Ring buffer for user-mode keyboard input
void keyboard_buffer_init(void);
int keyboard_buffer_push(uint8_t key);
uint8_t keyboard_buffer_pop(void);
int keyboard_buffer_empty(void);
void keyboard_buffer_enable(int enable);
int keyboard_buffer_is_enabled(void);

#endif