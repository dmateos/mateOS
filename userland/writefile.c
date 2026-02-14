#include "syscalls.h"
#include "libc.h"

void _start(int argc, char **argv) {
    if (argc < 3) {
        print("usage: writefile <file> <text>\n");
        exit(1);
    }

    int fd = open(argv[1], O_CREAT | O_TRUNC | O_RDWR);
    if (fd < 0) {
        print("writefile: open failed: ");
        print(argv[1]);
        print("\n");
        exit(1);
    }

    int len = strlen(argv[2]);
    int n = fwrite(fd, argv[2], (unsigned int)len);
    if (n != len) {
        print("writefile: fwrite failed\n");
        close(fd);
        exit(1);
    }

    // Append newline for easier `cat` output.
    if (fwrite(fd, "\n", 1) != 1) {
        print("writefile: newline write failed\n");
        close(fd);
        exit(1);
    }

    close(fd);
    exit(0);
}
