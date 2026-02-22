#include "syscalls.h"
#include "libc.h"

static int run_prog_argv(const char *prog, const char **argv, int argc) {
    int pid = spawn_argv(prog, argv, argc);
    if (pid < 0) return -1;
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

    print("tccsmoke: start\n");

    {
        const char *a[] = { "tcc.elf", "-v", 0 };
        int rc = run_prog_argv("tcc.elf", a, 2);
        if (rc != 0) {
            print("tccsmoke: FAIL (tcc -v rc=");
            print_num(rc);
            print(")\n");
            finish_and_exit(1);
        }
    }

    {
        const char *a[] = { "tcc.elf", "-c", "test2.c", "-o", "tcc_ret.o", 0 };
        int rc = run_prog_argv("tcc.elf", a, 5);
        if (rc != 0) {
            print("tccsmoke: FAIL (tcc -c test2.c rc=");
            print_num(rc);
            print(")\n");
            finish_and_exit(1);
        }
    }

    {
        stat_t st;
        if (stat("tcc_ret.o", &st) < 0 || st.size == 0) {
            print("tccsmoke: FAIL (missing tcc_ret.o)\n");
            finish_and_exit(1);
        }
        print("tccsmoke: compile-only OK (tcc_ret.o size=");
        print_num((int)st.size);
        print(")\n");
    }

    // Test 3: full link (multi-file → executable)
    {
        const char *a[] = { "tcc.elf", "t3a.c", "t3b.c", "-o", "t3.elf", 0 };
        int rc = run_prog_argv("tcc.elf", a, 5);
        if (rc != 0) {
            print("tccsmoke: FAIL (tcc link t3 rc=");
            print_num(rc);
            print(")\n");
            finish_and_exit(1);
        }
    }

    {
        stat_t st;
        if (stat("t3.elf", &st) < 0 || st.size == 0) {
            print("tccsmoke: FAIL (missing t3.elf)\n");
            finish_and_exit(1);
        }
        print("tccsmoke: link OK (t3.elf size=");
        print_num((int)st.size);
        print(")\n");
    }

    // Test 4: run the compiled program
    {
        const char *a[] = { "t3.elf", 0 };
        int rc = run_prog_argv("t3.elf", a, 1);
        if (rc != 0) {
            print("tccsmoke: FAIL (run t3.elf rc=");
            print_num(rc);
            print(")\n");
            finish_and_exit(1);
        }
    }

    print("tccsmoke: PASS\n");
    finish_and_exit(0);
}
