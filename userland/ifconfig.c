#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        // Show current config
        unsigned int ip_be = 0, mask_be = 0, gw_be = 0;
        if (net_get(&ip_be, &mask_be, &gw_be) != 0) {
            print("ifconfig: failed to read config\n");
            exit(1);
        }
        char buf[16];
        print("ip   ");
        format_ip4(ip_be, buf);
        print(buf);
        print("\n");
        print("mask ");
        format_ip4(mask_be, buf);
        print(buf);
        print("\n");
        print("gw   ");
        format_ip4(gw_be, buf);
        print(buf);
        print("\n");
        unsigned int rx = 0, tx = 0;
        if (net_stats(&rx, &tx) == 0) {
            print("rxpk ");
            print_num((int)rx);
            print("\n");
            print("txpk ");
            print_num((int)tx);
            print("\n");
        }
        exit(0);
    }

    if (argc >= 2 && strcmp(argv[1], "dhcp") == 0) {
        net_cfg(0, 0, 0);
        print("ifconfig: dhcp requested\n");
        exit(0);
    }

    if (argc < 4) {
        print("usage: ifconfig <ip> <mask> <gateway> | ifconfig dhcp\n");
        exit(1);
    }

    unsigned int ip_be, mask_be, gw_be;
    if (parse_ip4(argv[1], &ip_be) != 0) {
        print("ifconfig: invalid ip\n");
        exit(1);
    }
    if (parse_ip4(argv[2], &mask_be) != 0) {
        print("ifconfig: invalid mask\n");
        exit(1);
    }
    if (parse_ip4(argv[3], &gw_be) != 0) {
        print("ifconfig: invalid gateway\n");
        exit(1);
    }
    net_cfg(ip_be, mask_be, gw_be);
    print("ifconfig ok\n");
    exit(0);
}
