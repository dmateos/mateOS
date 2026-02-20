#include "syscalls.h"
#include "libc.h"

static void usage(void) {
    print("usage: cc <input.c> [-o output.elf] [--keep-temps]\n");
}

static char lower_ch(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int starts_with(const char *s, const char *pfx) {
    while (*pfx) {
        if (*s++ != *pfx++) return 0;
    }
    return 1;
}

static int ends_with(const char *s, const char *sfx) {
    int ls = strlen(s);
    int lx = strlen(sfx);
    if (lx > ls) return 0;
    return strcmp(s + (ls - lx), sfx) == 0;
}

static int ends_with_ci(const char *s, const char *sfx) {
    int ls = strlen(s);
    int lx = strlen(sfx);
    if (lx > ls) return 0;
    const char *a = s + (ls - lx);
    for (int i = 0; i < lx; i++) {
        if (lower_ch(a[i]) != lower_ch(sfx[i])) return 0;
    }
    return 1;
}

static int is_cc_temp_name(const char *name) {
    // Match cc_<digits>.* temp files.
    int i = 0;
    if ((name[0] == 'c' || name[0] == 'C') &&
        (name[1] == 'c' || name[1] == 'C') &&
        name[2] == '_') {
        i = 3;
    } else {
        return 0;
    }
    int have_digit = 0;
    while (name[i] >= '0' && name[i] <= '9') {
        i++;
        have_digit = 1;
    }
    if (!have_digit) return 0;
    if (!(ends_with_ci(name, ".asm") || ends_with_ci(name, ".obj") || ends_with_ci(name, ".bin"))) return 0;
    return 1;
}

static void cleanup_stale_cc_temps(void) {
    char name[64];
    int i = 0;
    while (readdir((unsigned int)i, name, sizeof(name)) > 0) {
        if (is_cc_temp_name(name)) {
            unlink(name);
        }
        i++;
    }
}

static void append_str(char *dst, int cap, const char *src) {
    int n = strlen(dst);
    int i = 0;
    while (src[i] && n + i + 1 < cap) {
        dst[n + i] = src[i];
        i++;
    }
    dst[n + i] = '\0';
}

static int run_stage(const char *prog, const char **argv, int argc) {
    int pid = spawn_argv(prog, argv, argc);
    if (pid < 0) {
        print("cc: failed to spawn ");
        print(prog);
        print("\n");
        return -1;
    }
    int rc = wait(pid);
    if (rc != 0) {
        print("cc: stage failed: ");
        print(prog);
        print(" (exit ");
        print_num(rc);
        print(")\n");
        return -1;
    }
    return 0;
}

static int require_nonempty_file(const char *path, const char *stage_name) {
    stat_t st;
    if (stat(path, &st) < 0) {
        print("cc: ");
        print(stage_name);
        print(" did not produce file: ");
        print(path);
        print("\n");
        return -1;
    }
    if (st.size == 0) {
        print("cc: ");
        print(stage_name);
        print(" produced empty file: ");
        print(path);
        print("\n");
        return -1;
    }
    return 0;
}

static void cleanup_tmp_files(const char *a, const char *b) {
    if (a && a[0]) unlink(a);
    if (b && b[0]) unlink(b);
}

static int ensure_runtime_obj(void) {
    static const char *rt_asm = "ccrt.asm";
    static const char *rt_obj = "ccrt.obj";
    stat_t st;
    if (stat(rt_obj, &st) == 0 && st.size > 0) {
        return 0;
    }
    if (require_nonempty_file(rt_asm, "runtime asm") != 0) return -1;
    {
        const char *a[] = { "as86.elf", "-f", "obj", "--org", "0x700000", "-o", rt_obj, rt_asm, 0 };
        if (run_stage("as86.elf", a, 8) != 0) return -1;
    }
    if (require_nonempty_file(rt_obj, "runtime obj") != 0) return -1;
    return 0;
}

void _start(int argc, char **argv) {
    const char *input = 0;
    const char *output = 0;
    int keep_temps = 0;
    char out_buf[160];
    char pid_buf[16];
    char app_asm_tmp[64];
    char app_obj_tmp[64];

    if (argc < 2) {
        usage();
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                usage();
                exit(1);
            }
            output = argv[++i];
            continue;
        }
        if (strcmp(a, "--keep-temps") == 0) {
            keep_temps = 1;
            continue;
        }
        if (starts_with(a, "-")) {
            print("cc: unknown option: ");
            print(a);
            print("\n");
            usage();
            exit(1);
        }
        if (!input) {
            input = a;
        } else {
            print("cc: multiple input files are not supported yet\n");
            exit(1);
        }
    }

    if (!input) {
        usage();
        exit(1);
    }

    if (!output) {
        memset(out_buf, 0, sizeof(out_buf));
        append_str(out_buf, sizeof(out_buf), input);
        if (ends_with(out_buf, ".c")) {
            int n = strlen(out_buf);
            out_buf[n - 2] = '\0';
        } else if (ends_with(out_buf, ".C")) {
            int n = strlen(out_buf);
            out_buf[n - 2] = '\0';
        }
        append_str(out_buf, sizeof(out_buf), ".elf");
        output = out_buf;
    }

    itoa(getpid(), pid_buf);
    memset(app_asm_tmp, 0, sizeof(app_asm_tmp));
    memset(app_obj_tmp, 0, sizeof(app_obj_tmp));
    append_str(app_asm_tmp, sizeof(app_asm_tmp), "cc_");
    append_str(app_asm_tmp, sizeof(app_asm_tmp), pid_buf);
    append_str(app_asm_tmp, sizeof(app_asm_tmp), ".asm");
    append_str(app_obj_tmp, sizeof(app_obj_tmp), "cc_");
    append_str(app_obj_tmp, sizeof(app_obj_tmp), pid_buf);
    append_str(app_obj_tmp, sizeof(app_obj_tmp), ".obj");

    cleanup_stale_cc_temps();
    if (ensure_runtime_obj() != 0) {
        if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
        exit(1);
    }

    {
        const char *a1[] = {
            "smallerc.elf",
            "-seg32",
            "-no-leading-underscore",
            input,
            app_asm_tmp,
            0
        };
        if (run_stage("smallerc.elf", a1, 5) != 0) {
            if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
            exit(1);
        }
        if (require_nonempty_file(app_asm_tmp, "smallerc") != 0) {
            if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
            exit(1);
        }
    }
    {
        const char *a2[] = { "as86.elf", "-f", "obj", "--org", "0x700000", "-o", app_obj_tmp, app_asm_tmp, 0 };
        if (run_stage("as86.elf", a2, 8) != 0) {
            if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
            exit(1);
        }
        if (require_nonempty_file(app_obj_tmp, "as86(app)") != 0) {
            if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
            exit(1);
        }
    }
    {
        // Runtime object first so ld86 default entry points at $_start.
        const char *a4[] = { "ld86.elf", "-o", output, "ccrt.obj", app_obj_tmp, 0 };
        if (run_stage("ld86.elf", a4, 5) != 0) {
            if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
            exit(1);
        }
    }

    if (!keep_temps) cleanup_tmp_files(app_asm_tmp, app_obj_tmp);
    else {
        print("cc: temp files: ");
        print(app_asm_tmp);
        print(" ");
        print(app_obj_tmp);
        print("\n");
    }

    print("cc: built ");
    print(output);
    print("\n");
    exit(0);
}
