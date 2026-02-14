#include "syscalls.h"
#include "libc.h"

static void print_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("cannot open ");
        print(path);
        print("\n");
        return;
    }
    char buf[256];
    while (1) {
        int n = fread(fd, buf, sizeof(buf));
        if (n <= 0) break;
        write(1, buf, (unsigned int)n);
    }
    close(fd);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_file("/meminfo.ker");
    exit(0);
}
