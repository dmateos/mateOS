#include "syscalls.h"
#include "libc.h"

#define MAX_INPUTS 16
#define IN_C 1
#define IN_OBJ 2
#define IN_LIB 3

static void usage(void) {
    print("usage: cc [options] <inputs>\n");
    print("  link: cc a.c b.c x.o lib.a -o app.elf\n");
    print("  asm:  cc -S a.c [-o a.asm]\n");
    print("  obj:  cc -c a.c [-o a.o]\n");
    print("options: -o <out> -c -S --keep-temps\n");
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
    // Match cc_<digits>[ _<digits> ].(asm|obj|bin) temp files.
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
    if (name[i] == '_') {
        i++;
        have_digit = 0;
        while (name[i] >= '0' && name[i] <= '9') {
            i++;
            have_digit = 1;
        }
        if (!have_digit) return 0;
    }
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

static int ensure_runtime_inputs(void) {
    static const char *crt0_obj = "crt0.o";
    static const char *libc_obj = "libc.o";
    static const char *sys_obj = "syscalls.o";
    stat_t st;
    if (stat(crt0_obj, &st) != 0 || st.size == 0) return -1;
    if (stat(libc_obj, &st) != 0 || st.size == 0) return -1;
    if (stat(sys_obj, &st) != 0 || st.size == 0) return -1;
    return 0;
}

static int infer_input_kind(const char *path) {
    if (ends_with_ci(path, ".c")) return IN_C;
    if (ends_with_ci(path, ".o") || ends_with_ci(path, ".obj")) return IN_OBJ;
    if (ends_with_ci(path, ".a")) return IN_LIB;
    return 0;
}

static void derive_out_from_input(const char *in, const char *ext, char *out, int out_cap) {
    memset(out, 0, (unsigned int)out_cap);
    append_str(out, out_cap, in);
    if (ends_with_ci(out, ".c") || ends_with_ci(out, ".o") || ends_with_ci(out, ".a") || ends_with_ci(out, ".obj")) {
        int n = strlen(out);
        int cut = 0;
        if (ends_with_ci(out, ".obj")) cut = 4;
        else cut = 2;
        out[n - cut] = '\0';
    }
    append_str(out, out_cap, ext);
}

void _start(int argc, char **argv) {
    const char *inputs[MAX_INPUTS];
    int input_kind[MAX_INPUTS];
    int input_count = 0;
    const char *output = 0;
    int keep_temps = 0;
    int opt_c = 0;
    int opt_S = 0;
    char out_buf[160];
    char pid_buf[16];
    char app_asm_tmp[MAX_INPUTS][64];
    char app_obj_tmp[MAX_INPUTS][64];

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
        if (strcmp(a, "-c") == 0) {
            opt_c = 1;
            continue;
        }
        if (strcmp(a, "-S") == 0) {
            opt_S = 1;
            continue;
        }
        if (starts_with(a, "-")) {
            print("cc: unknown option: ");
            print(a);
            print("\n");
            usage();
            exit(1);
        }
        if (input_count >= MAX_INPUTS) {
            print("cc: too many input files\n");
            exit(1);
        }
        int kind = infer_input_kind(a);
        if (!kind) {
            print("cc: unsupported input type: ");
            print(a);
            print("\n");
            exit(1);
        }
        inputs[input_count++] = a;
        input_kind[input_count - 1] = kind;
    }

    if (input_count <= 0) {
        usage();
        exit(1);
    }

    if (opt_c && opt_S) {
        print("cc: cannot use -c and -S together\n");
        exit(1);
    }

    if (opt_c || opt_S) {
        for (int i = 0; i < input_count; i++) {
            if (input_kind[i] != IN_C) {
                print("cc: -c/-S accepts only .c inputs\n");
                exit(1);
            }
        }
    }

    if (!output) {
        if (input_count == 1) {
            const char *ext = ".elf";
            if (opt_c) ext = ".o";
            else if (opt_S) ext = ".asm";
            derive_out_from_input(inputs[0], ext, out_buf, sizeof(out_buf));
            output = out_buf;
        } else {
            if (opt_c || opt_S) {
                output = 0;
            } else {
                print("cc: -o is required when linking multiple inputs\n");
                exit(1);
            }
        }
    }

    itoa(getpid(), pid_buf);
    memset(app_asm_tmp, 0, sizeof(app_asm_tmp));
    memset(app_obj_tmp, 0, sizeof(app_obj_tmp));
    for (int i = 0; i < input_count; i++) {
        char idx_buf[16];
        itoa(i, idx_buf);
        append_str(app_asm_tmp[i], sizeof(app_asm_tmp[i]), "cc_");
        append_str(app_asm_tmp[i], sizeof(app_asm_tmp[i]), pid_buf);
        append_str(app_asm_tmp[i], sizeof(app_asm_tmp[i]), "_");
        append_str(app_asm_tmp[i], sizeof(app_asm_tmp[i]), idx_buf);
        append_str(app_asm_tmp[i], sizeof(app_asm_tmp[i]), ".asm");
        append_str(app_obj_tmp[i], sizeof(app_obj_tmp[i]), "cc_");
        append_str(app_obj_tmp[i], sizeof(app_obj_tmp[i]), pid_buf);
        append_str(app_obj_tmp[i], sizeof(app_obj_tmp[i]), "_");
        append_str(app_obj_tmp[i], sizeof(app_obj_tmp[i]), idx_buf);
        append_str(app_obj_tmp[i], sizeof(app_obj_tmp[i]), ".obj");
    }

    cleanup_stale_cc_temps();

    for (int i = 0; i < input_count; i++) {
        if (input_kind[i] != IN_C) continue;
        const char *a1[] = {
            "smallerc.elf",
            "-seg32",
            "-no-leading-underscore",
            inputs[i],
            app_asm_tmp[i],
            0
        };
        if (run_stage("smallerc.elf", a1, 5) != 0) {
            if (!keep_temps) {
                for (int j = 0; j < input_count; j++) cleanup_tmp_files(app_asm_tmp[j], app_obj_tmp[j]);
            }
            exit(1);
        }
        if (require_nonempty_file(app_asm_tmp[i], "smallerc") != 0) {
            if (!keep_temps) {
                for (int j = 0; j < input_count; j++) cleanup_tmp_files(app_asm_tmp[j], app_obj_tmp[j]);
            }
            exit(1);
        }
    }
    if (opt_S) {
        for (int i = 0; i < input_count; i++) {
            char final_asm[160];
            const char *dst = 0;
            if (output && input_count == 1) dst = output;
            else {
                derive_out_from_input(inputs[i], ".asm", final_asm, sizeof(final_asm));
                dst = final_asm;
            }
            if (strcmp(app_asm_tmp[i], dst) != 0) {
                if (unlink(dst) < 0) { /* ignore missing */ }
                int infd = open(app_asm_tmp[i], O_RDONLY);
                int outfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
                if (infd < 0 || outfd < 0) {
                    print("cc: cannot write asm output\n");
                    exit(1);
                }
                char buf[512];
                int rn;
                while ((rn = fread(infd, buf, sizeof(buf))) > 0) {
                    if (fwrite(outfd, buf, (unsigned int)rn) != rn) {
                        close(infd); close(outfd);
                        print("cc: asm copy failed\n");
                        exit(1);
                    }
                }
                close(infd); close(outfd);
                unlink(app_asm_tmp[i]);
            }
            if (require_nonempty_file(dst, "asm output") != 0) exit(1);
        }
        print("cc: built asm output\n");
        exit(0);
    }

    for (int i = 0; i < input_count; i++) {
        if (input_kind[i] != IN_C) continue;
        const char *a2[] = { "as86.elf", "-f", "obj", "--org", "0x700000", "-o", app_obj_tmp[i], app_asm_tmp[i], 0 };
        if (run_stage("as86.elf", a2, 8) != 0) {
            if (!keep_temps) {
                for (int j = 0; j < input_count; j++) cleanup_tmp_files(app_asm_tmp[j], app_obj_tmp[j]);
            }
            exit(1);
        }
        if (require_nonempty_file(app_obj_tmp[i], "as86(app)") != 0) {
            if (!keep_temps) {
                for (int j = 0; j < input_count; j++) cleanup_tmp_files(app_asm_tmp[j], app_obj_tmp[j]);
            }
            exit(1);
        }
    }
    if (opt_c) {
        for (int i = 0; i < input_count; i++) {
            char final_obj[160];
            const char *dst = 0;
            if (output && input_count == 1) dst = output;
            else {
                derive_out_from_input(inputs[i], ".o", final_obj, sizeof(final_obj));
                dst = final_obj;
            }
            if (strcmp(app_obj_tmp[i], dst) != 0) {
                if (unlink(dst) < 0) { /* ignore missing */ }
                int infd = open(app_obj_tmp[i], O_RDONLY);
                int outfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
                if (infd < 0 || outfd < 0) {
                    print("cc: cannot write object output\n");
                    exit(1);
                }
                char buf[512];
                int rn;
                while ((rn = fread(infd, buf, sizeof(buf))) > 0) {
                    if (fwrite(outfd, buf, (unsigned int)rn) != rn) {
                        close(infd); close(outfd);
                        print("cc: object copy failed\n");
                        exit(1);
                    }
                }
                close(infd); close(outfd);
                unlink(app_obj_tmp[i]);
            }
            if (!keep_temps) unlink(app_asm_tmp[i]);
            if (require_nonempty_file(dst, "object output") != 0) exit(1);
        }
        print("cc: built object output\n");
        exit(0);
    }

    if (ensure_runtime_inputs() != 0) {
        if (!keep_temps) {
            for (int i = 0; i < input_count; i++) cleanup_tmp_files(app_asm_tmp[i], app_obj_tmp[i]);
        }
        print("cc: missing runtime objects (crt0.o/libc.o/syscalls.o)\n");
        exit(1);
    }
    {
        const char *a4[3 + 1 + (MAX_INPUTS * 2) + 4];
        int n = 0;
        a4[n++] = "ld86.elf";
        a4[n++] = "-o";
        a4[n++] = output;
        // Keep crt0 first so ld86 default entry points at $_start.
        a4[n++] = "crt0.o";
        for (int i = 0; i < input_count; i++) {
            if (input_kind[i] == IN_C) a4[n++] = app_obj_tmp[i];
            else a4[n++] = inputs[i];
        }
        a4[n++] = "libc.o";
        a4[n++] = "syscalls.o";
        a4[n] = 0;
        if (run_stage("ld86.elf", a4, n) != 0) {
            if (!keep_temps) {
                for (int j = 0; j < input_count; j++) cleanup_tmp_files(app_asm_tmp[j], app_obj_tmp[j]);
            }
            exit(1);
        }
    }

    if (!keep_temps) {
        for (int i = 0; i < input_count; i++) cleanup_tmp_files(app_asm_tmp[i], app_obj_tmp[i]);
    }
    else {
        print("cc: temp files: ");
        for (int i = 0; i < input_count; i++) {
            if (i) print(" ");
            print(app_asm_tmp[i]);
            print(" ");
            print(app_obj_tmp[i]);
        }
        print("\n");
    }

    print("cc: built ");
    print(output);
    print("\n");
    exit(0);
}
