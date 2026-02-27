#include "strbuf.h"

int strbuf_append_char(char *dst, uint32_t cap, uint32_t *len, char c) {
    if (!dst || !len)
        return -1;
    if (*len >= cap)
        return -1;
    dst[*len] = c;
    (*len)++;
    return 0;
}

int strbuf_append_cstr(char *dst, uint32_t cap, uint32_t *len, const char *s) {
    if (!s)
        return -1;
    while (*s) {
        if (strbuf_append_char(dst, cap, len, *s++) < 0)
            return -1;
    }
    return 0;
}

int strbuf_append_dec_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v) {
    if (v == 0)
        return strbuf_append_char(dst, cap, len, '0');

    char tmp[16];
    int t = 0;
    while (v > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = t - 1; i >= 0; i--) {
        if (strbuf_append_char(dst, cap, len, tmp[i]) < 0)
            return -1;
    }
    return 0;
}

int strbuf_append_hex_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v) {
    if (strbuf_append_cstr(dst, cap, len, "0x") < 0)
        return -1;

    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nib = (v >> (uint32_t)shift) & 0xF;
        if (!started && nib == 0 && shift > 0)
            continue;
        started = 1;
        char c = (char)(nib < 10 ? ('0' + nib) : ('a' + (nib - 10)));
        if (strbuf_append_char(dst, cap, len, c) < 0)
            return -1;
    }

    if (!started) {
        if (strbuf_append_char(dst, cap, len, '0') < 0)
            return -1;
    }
    return 0;
}
