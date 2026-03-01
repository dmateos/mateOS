#include "libc.h"
#include "syscalls.h"

static int run_prog_argv(const char *prog, const char **argv, int argc) {
    int pid = spawn_argv(prog, argv, argc);
    if (pid < 0)
        return -1;
    return wait(pid);
}

static void finish_and_exit(int rc) {
    debug_exit(rc);
    shutdown();
    exit(rc);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("ccsymtest: start\n");
    {
        const char *a[] = {"bin/cc.elf", "t3a.c", "t3b.c", "-o", "ccmul.elf", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 5);
        if (rc != 0) {
            print("ccsymtest: FAIL (cc rc=");
            print_num(rc);
            print(")\n");
            finish_and_exit(1);
        }
    }

    {
        stat_t st;
        if (stat("ccmul.elf", &st) < 0 || st.size == 0) {
            print("ccsymtest: FAIL (missing ccmul.elf)\n");
            finish_and_exit(1);
        }
    }

    print("ccsymtest: PASS\n");
    finish_and_exit(0);
}
