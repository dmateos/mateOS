#include "syscalls.h"
#include "libc.h"

void _start(int argc, char **argv) {
    if (argc < 3) {
        print("usage: cp <src> <dst>\n");
        exit(1);
    }

    int in = open(argv[1], O_RDONLY);
    if (in < 0) {
        print("cp: open src failed: ");
        print(argv[1]);
        print("\n");
        exit(1);
    }

    int out = open(argv[2], O_CREAT | O_TRUNC | O_RDWR);
    if (out < 0) {
        print("cp: open dst failed: ");
        print(argv[2]);
        print("\n");
        close(in);
        exit(1);
    }

    char buf[256];
    while (1) {
        int n = fd_read(in, buf, sizeof(buf));
        if (n < 0) {
            print("cp: read failed\n");
            close(in);
            close(out);
            exit(1);
        }
        if (n == 0) break;

        int off = 0;
        while (off < n) {
            int w = fd_write(out, buf + off, (unsigned int)(n - off));
            if (w <= 0) {
                print("cp: write failed\n");
                close(in);
                close(out);
                exit(1);
            }
            off += w;
        }
    }

    close(in);
    close(out);
    exit(0);
}
