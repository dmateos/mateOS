#include "syscalls.h"
#include "libc.h"

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
