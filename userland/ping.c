#include "syscalls.h"

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    write(1, s, slen(s));
}

static int parse_ip4(const char *s, unsigned int *out_be) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    int part = 0;
    unsigned int val = 0;
    for (int i = 0; ; i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            val = val * 10 + (unsigned int)(ch - '0');
            if (val > 255) return -1;
        } else if (ch == '.' || ch == '\0') {
            if (part == 0) a = val;
            else if (part == 1) b = val;
            else if (part == 2) c = val;
            else if (part == 3) d = val;
            else return -1;
            part++;
            val = 0;
            if (ch == '\0') break;
        } else {
            return -1;
        }
    }
    if (part != 4) return -1;
    *out_be = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("usage: ping <ip>\n");
        exit(1);
    }

    unsigned int ip_be;
    if (parse_ip4(argv[1], &ip_be) != 0) {
        print("ping: invalid ip\n");
        exit(1);
    }

    if (net_ping(ip_be, 1000) == 0) {
        print("ping ");
        print(argv[1]);
        print(": ok\n");
    } else {
        print("ping ");
        print(argv[1]);
        print(": timeout\n");
    }
    exit(0);
}
