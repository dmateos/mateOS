#include "kring.h"

void kring_u8_init(kring_u8_t *r, uint8_t *storage, uint32_t capacity) {
  if (!r) return;
  r->buf = storage;
  r->capacity = capacity;
  r->head = 0;
  r->tail = 0;
}

void kring_u8_reset(kring_u8_t *r) {
  if (!r) return;
  r->head = 0;
  r->tail = 0;
}

int kring_u8_push(kring_u8_t *r, uint8_t value) {
  if (!r || !r->buf || r->capacity < 2) return -1;

  uint32_t next = (r->head + 1) % r->capacity;
  if (next == r->tail) return -1;  // Full

  r->buf[r->head] = value;
  r->head = next;
  return 0;
}

int kring_u8_pop(kring_u8_t *r, uint8_t *out) {
  if (!r || !r->buf || !out || r->capacity < 2) return -1;
  if (r->head == r->tail) return -1;  // Empty

  *out = r->buf[r->tail];
  r->tail = (r->tail + 1) % r->capacity;
  return 0;
}

int kring_u8_empty(const kring_u8_t *r) {
  if (!r || !r->buf || r->capacity < 2) return 1;
  return r->head == r->tail;
}

uint32_t kring_u8_used(const kring_u8_t *r) {
  if (!r || !r->buf || r->capacity < 2) return 0;
  if (r->head >= r->tail) return r->head - r->tail;
  return r->capacity - (r->tail - r->head);
}
