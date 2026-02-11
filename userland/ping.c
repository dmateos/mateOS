// Simple ping tool (fixed target: 10.0.2.2)
#include "syscalls.h"

static unsigned int ustrlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    write(1, s, ustrlen(s));
}

void _start(void) {
    unsigned int ip = (10u << 24) | (0u << 16) | (2u << 8) | 2u;
    int r = net_ping(ip, 1000);
    if (r == 0) {
        print("ping 10.0.2.2: ok\n");
    } else {
        print("ping 10.0.2.2: timeout\n");
    }
    exit(0);
}
