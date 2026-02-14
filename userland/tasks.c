#include "syscalls.h"
#include "libc.h"

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    taskinfo_entry_t tlist[16];
    int count = tasklist(tlist, 16);
    print("PID  PPID  Ring  State    Name\n");
    print("---  ----  ----  -------  ----\n");
    for (int i = 0; i < count; i++) {
        print_num((int)tlist[i].id);
        print("    ");
        print_num((int)tlist[i].parent_id);
        print("    ");
        print_num((int)tlist[i].ring);
        print("    ");
        switch (tlist[i].state) {
            case 0: print("ready  "); break;
            case 1: print("run    "); break;
            case 2: print("block  "); break;
            default: print("???    "); break;
        }
        print("  ");
        print(tlist[i].name);
        print("\n");
    }
    exit(0);
}
