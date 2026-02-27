#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: rmdir <path>\n");
        exit(1);
    }
    if (rmdir(argv[1]) < 0) {
        print("rmdir: failed (not empty or not found): ");
        print(argv[1]);
        print("\n");
        exit(1);
    }
    exit(0);
}
