#include "libc.h"
#include "syscalls.h"

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    taskinfo_entry_t tlist[16];
    int count = tasklist(tlist, 16);
    unsigned int now = get_ticks();

    print("PID  PPID  R  STATE   CPU%  START  NAME\n");
    print("---  ----  -  -----   ----  -----  ----\n");
    for (int i = 0; i < count; i++) {
        // Lifetime CPU% = runtime_ticks / age_ticks * 100
        unsigned int age = (tlist[i].start_ticks < now)
                           ? (now - tlist[i].start_ticks) : 0;
        unsigned int cpu_pct = (age > 0)
                               ? (tlist[i].runtime_ticks * 100u / age) : 0;
        if (cpu_pct > 100) cpu_pct = 100;
        // Seconds since spawn
        unsigned int start_sec = tlist[i].start_ticks / 100;

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
        print_num((int)cpu_pct);
        print("%   ");
        print_num((int)start_sec);
        print("s  ");
        print(tlist[i].name);
        print("\n");
    }
    exit(0);
}
