#include "syscalls.h"

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    write(1, s, slen(s));
}

static void print_num(int n) {
    if (n < 0) { write(1, "-", 1); n = -n; }
    if (n == 0) { write(1, "0", 1); return; }
    char buf[12];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) write(1, &buf[--i], 1);
}

static void print_ip(unsigned int ip_be) {
    print_num((int)((ip_be >> 24) & 0xFF)); print(".");
    print_num((int)((ip_be >> 16) & 0xFF)); print(".");
    print_num((int)((ip_be >> 8) & 0xFF));  print(".");
    print_num((int)(ip_be & 0xFF));
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
        } else if (ch == '.' || ch == '\0' || ch == ' ') {
            if (part == 0) a = val;
            else if (part == 1) b = val;
            else if (part == 2) c = val;
            else if (part == 3) d = val;
            else return -1;
            part++;
            val = 0;
            if (ch == '\0' || ch == ' ') break;
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
        // Show current config
        unsigned int ip_be = 0, mask_be = 0, gw_be = 0;
        if (net_get(&ip_be, &mask_be, &gw_be) != 0) {
            print("ifconfig: failed to read config\n");
            exit(1);
        }
        print("ip   "); print_ip(ip_be); print("\n");
        print("mask "); print_ip(mask_be); print("\n");
        print("gw   "); print_ip(gw_be); print("\n");
        exit(0);
    }

    if (argc < 4) {
        print("usage: ifconfig <ip> <mask> <gateway>\n");
        exit(1);
    }

    unsigned int ip_be, mask_be, gw_be;
    if (parse_ip4(argv[1], &ip_be) != 0) {
        print("ifconfig: invalid ip\n"); exit(1);
    }
    if (parse_ip4(argv[2], &mask_be) != 0) {
        print("ifconfig: invalid mask\n"); exit(1);
    }
    if (parse_ip4(argv[3], &gw_be) != 0) {
        print("ifconfig: invalid gateway\n"); exit(1);
    }
    net_cfg(ip_be, mask_be, gw_be);
    print("ifconfig ok\n");
    exit(0);
}
