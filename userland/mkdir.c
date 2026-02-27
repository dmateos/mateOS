#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: mkdir <path>\n");
        exit(1);
    }
    if (mkdir(argv[1]) < 0) {
        print("mkdir: failed to create directory: ");
        print(argv[1]);
        print("\n");
        exit(1);
    }
    exit(0);
}
