#include "syscalls.h"
#include "libc.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("usage: touch <file>\n");
        exit(1);
    }

    int fd = open(argv[1], O_CREAT | O_RDWR);
    if (fd < 0) {
        print("touch: open failed: ");
        print(argv[1]);
        print("\n");
        exit(1);
    }
    close(fd);
    exit(0);
}
