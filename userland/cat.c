#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("usage: cat <file>\n");
        exit(1);
    }

    int fd = open(argv[1], 0);
    if (fd < 0) {
        print("cat: file not found: ");
        print(argv[1]);
        print("\n");
        exit(1);
    }

    char buf[256];
    int n;
    while ((n = fd_read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
    close(fd);
    exit(0);
}
