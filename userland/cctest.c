#include "libc.h"
#include "syscalls.h"

static int run_prog_argv(const char *prog, const char **argv, int argc) {
    int pid = spawn_argv(prog, argv, argc);
    if (pid < 0)
        return -1;
    return wait(pid);
}

static int run_prog(const char *prog) {
    int pid = spawn(prog);
    if (pid < 0)
        return -1;
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

static int require_file_nonempty(const char *path) {
    stat_t st;
    if (stat(path, &st) < 0)
        return -1;
    return (st.size > 0) ? 0 : -1;
}

static void finish_and_exit(int rc) {
    // For automated host-side smoke runs under QEMU with:
    //   -device isa-debug-exit,iobase=0xf4,iosize=0x04
    // this provides a machine-readable result.
    // rc=0 => host sees qemu exit status 1.
    debug_exit(rc);
    shutdown();
    exit(rc);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("cctest: compiler smoke start\n");

    {
        const char *a[] = {"bin/cc.elf", "test2.c", "-o", "cc_ret.elf", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 4);
        if (rc != 0)
            finish_and_exit(fail("cc test2.c", rc));
    }
    {
        int rc = run_prog("cc_ret.elf");
        if (rc != 0)
            finish_and_exit(fail("run cc_ret.elf", rc));
    }

    {
        const char *a[] = {"bin/cc.elf", "test.c", "-o", "cc_print.elf", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 4);
        if (rc != 0)
            finish_and_exit(fail("cc test.c", rc));
    }
    {
        int rc = run_prog("cc_print.elf");
        if (rc != 0)
            finish_and_exit(fail("run cc_print.elf", rc));
    }

    {
        const char *a[] = {"bin/cc.elf", "-S", "test2.c", "-o", "cc_s.asm", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 5);
        if (rc != 0)
            finish_and_exit(fail("cc -S test2.c", rc));
        if (require_file_nonempty("cc_s.asm") != 0)
            finish_and_exit(fail("missing cc_s.asm", -1));
    }

    {
        const char *a[] = {"bin/cc.elf", "-c", "test2.c", "-o", "cc_c.o", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 5);
        if (rc != 0)
            finish_and_exit(fail("cc -c test2.c", rc));
        if (require_file_nonempty("cc_c.o") != 0)
            finish_and_exit(fail("missing cc_c.o", -1));
    }
    {
        const char *a[] = {"bin/cc.elf", "cc_c.o", "-o", "cc_obj.elf", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 4);
        if (rc != 0)
            finish_and_exit(fail("cc cc_c.o", rc));
    }
    {
        int rc = run_prog("cc_obj.elf");
        if (rc != 0)
            finish_and_exit(fail("run cc_obj.elf", rc));
    }

    {
        const char *a[] = {"bin/cc.elf", "t3a.c", "t3b.c", "-o", "ccmul.elf", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 5);
        if (rc != 0)
            finish_and_exit(fail("cc t3a.c t3b.c", rc));
    }
    {
        int rc = run_prog("ccmul.elf");
        if (rc != 0)
            finish_and_exit(fail("run ccmul.elf", rc));
    }

    {
        const char *a[] = {"bin/cc.elf", "t4.c",       "lib/libtiny.a",
                           "-o",         "cc_lib.elf", 0};
        int rc = run_prog_argv("bin/cc.elf", a, 5);
        if (rc != 0)
            finish_and_exit(fail("cc t4.c libtiny.a", rc));
    }
    {
        int rc = run_prog("cc_lib.elf");
        if (rc != 0)
            finish_and_exit(fail("run cc_lib.elf", rc));
    }

    print("cctest: PASS\n");
    finish_and_exit(0);
}
