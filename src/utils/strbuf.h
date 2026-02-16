#ifndef _STRBUF_H
#define _STRBUF_H

#include "../lib.h"

int strbuf_append_char(char *dst, uint32_t cap, uint32_t *len, char c);
int strbuf_append_cstr(char *dst, uint32_t cap, uint32_t *len, const char *s);
int strbuf_append_dec_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v);
int strbuf_append_hex_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v);

#endif
