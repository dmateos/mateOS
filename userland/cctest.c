#include "syscalls.h"
#include "libc.h"

static int run_prog_argv(const char *prog, const char **argv, int argc) {
    int pid = spawn_argv(prog, argv, argc);
    if (pid < 0) return -1;
    return wait(pid);
}

static int run_prog(const char *prog) {
    int pid = spawn(prog);
    if (pid < 0) return -1;
    return wait(pid);
}

static int fail(const char *msg, int rc) {
    print("cctest: FAIL: ");
    print(msg);
    if (rc != 0) {
        print(" (rc=");
        print_num(rc);
        print(")");
    }
    print("\n");
    return 1;
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("cctest: compiler smoke start\n");

    {
        const char *a[] = { "cc.elf", "test2.c", "-o", "cc_ret.elf", 0 };
        int rc = run_prog_argv("cc.elf", a, 4);
        if (rc != 0) exit(fail("cc test2.c", rc));
    }
    {
        int rc = run_prog("cc_ret.elf");
        if (rc != 0) exit(fail("run cc_ret.elf", rc));
    }

    {
        const char *a[] = { "cc.elf", "test.c", "-o", "cc_print.elf", 0 };
        int rc = run_prog_argv("cc.elf", a, 4);
        if (rc != 0) exit(fail("cc test.c", rc));
    }
    {
        int rc = run_prog("cc_print.elf");
        if (rc != 0) exit(fail("run cc_print.elf", rc));
    }

    print("cctest: PASS\n");
    exit(0);
}
