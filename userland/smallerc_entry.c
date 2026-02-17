#include "syscalls.h"

int main(int argc, char **argv);

void _start(int argc, char **argv) {
    int rc = main(argc, argv);
    exit(rc);
}
