#include "syscalls.h"
#include "libc.h"

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    unsigned int ticks = get_ticks();      // 100 Hz
    unsigned int total_seconds = ticks / 100;

    unsigned int days = total_seconds / 86400;
    unsigned int rem = total_seconds % 86400;
    unsigned int hours = rem / 3600;
    rem %= 3600;
    unsigned int minutes = rem / 60;
    unsigned int seconds = rem % 60;

    print("uptime: ");
    if (days > 0) {
        print_num((int)days);
        print("d ");
    }
    print_num((int)hours);
    print("h ");
    print_num((int)minutes);
    print("m ");
    print_num((int)seconds);
    print("s\n");

    exit(0);
}
