#ifndef _KRING_H
#define _KRING_H

#include "lib.h"

typedef struct {
  uint8_t *buf;
  uint32_t capacity;
  uint32_t head;
  uint32_t tail;
} kring_u8_t;

void kring_u8_init(kring_u8_t *r, uint8_t *storage, uint32_t capacity);
void kring_u8_reset(kring_u8_t *r);
int kring_u8_push(kring_u8_t *r, uint8_t value);
int kring_u8_pop(kring_u8_t *r, uint8_t *out);
int kring_u8_empty(const kring_u8_t *r);
uint32_t kring_u8_used(const kring_u8_t *r);

#endif
