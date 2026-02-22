typedef unsigned long long u64;

static u64 udivmod64(u64 n, u64 d, u64 *r) {
    u64 q = 0;
    u64 bit = 1;

    if (d == 0) {
        if (r) *r = 0;
        return 0;
    }

    while ((d & (1ULL << 63)) == 0 && d < n) {
        d <<= 1;
        bit <<= 1;
    }

    while (bit) {
        if (n >= d) {
            n -= d;
            q |= bit;
        }
        d >>= 1;
        bit >>= 1;
    }

    if (r) *r = n;
    return q;
}

u64 __udivdi3(u64 n, u64 d) {
    return udivmod64(n, d, 0);
}

u64 __umoddi3(u64 n, u64 d) {
    u64 r;
    (void)udivmod64(n, d, &r);
    return r;
}
