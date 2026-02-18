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
    // Match cc_<digits>.asm / cc_<digits>.obj / cc_<digits>.bin.
    if (!(name[0] == 'c' || name[0] == 'C')) return 0;
    if (!(name[1] == 'c' || name[1] == 'C')) return 0;
    if (name[2] != '_') return 0;
    int i = 3;
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

static void cleanup_tmp_pair(const char *asm_tmp, const char *bin_tmp) {
    unlink(asm_tmp);
    unlink(bin_tmp);
}

static int inject_runtime_asm(const char *path) {
    static const char *rt_prefix =
        "; ---- cc built-in crt0 ----\n"
        "bits 32\n"
        "section .text\n"
        "global $_start\n"
        "$_start:\n"
        "\tcall\t$main\n"
        "\tmov\tebx, eax\n"
        "\tmov\teax, 2\n"
        "\tint\t0x80\n"
        "..@cc_hang:\n"
        "\tjmp\t..@cc_hang\n"
        "\n";
    static const char *rt_suffix =
        "\n"
        "; ---- cc built-in runtime ----\n"
        "section .text\n"
        "global $print\n"
        "$print:\n"
        "\tpush\tebp\n"
        "\tmov\tebp, esp\n"
        "\tpush\tebx\n"
        "\tpush\tecx\n"
        "\tpush\tesi\n"
        "\tpush\tedx\n"
        "\tmov\tecx, [ebp+8]\n"
        "\tmov\tesi, ecx\n"
        "\txor\tedx, edx\n"
        "..@cc_strlen_loop:\n"
        "\tcmp\tbyte [esi], 0\n"
        "\tje\t..@cc_strlen_done\n"
        "\tinc\tesi\n"
        "\tinc\tedx\n"
        "\tjmp\t..@cc_strlen_loop\n"
        "..@cc_strlen_done:\n"
        "\tmov\teax, 1\n"
        "\tmov\tebx, 1\n"
        "\tint\t0x80\n"
        "\txor\teax, eax\n"
        "\tpop\tedx\n"
        "\tpop\tesi\n"
        "\tpop\tecx\n"
        "\tpop\tebx\n"
        "\tleave\n"
        "\tret\n";

    stat_t st;
    if (stat(path, &st) < 0 || st.size == 0) {
        print("cc: runtime inject stat failed\n");
        return -1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("cc: failed to open asm for runtime inject\n");
        return -1;
    }
    char *src = (char *)malloc(st.size + 1);
    if (!src) {
        close(fd);
        print("cc: out of memory in runtime inject\n");
        return -1;
    }
    int rn = fread(fd, src, st.size);
    close(fd);
    if (rn != (int)st.size) {
        print("cc: failed to read asm for runtime inject\n");
        free(src);
        return -1;
    }
    src[st.size] = '\0';
    int n2 = (int)st.size;

    fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        print("cc: failed to reopen asm for runtime inject\n");
        free(src);
        return -1;
    }

    int n1 = strlen(rt_prefix);
    int n3 = strlen(rt_suffix);
    int ok = 1;
    if (fwrite(fd, rt_prefix, (unsigned int)n1) != n1) ok = 0;
    if (ok && fwrite(fd, src, (unsigned int)n2) != n2) ok = 0;
    if (ok && fwrite(fd, rt_suffix, (unsigned int)n3) != n3) ok = 0;
    free(src);
    if (!ok) {
        close(fd);
        print("cc: failed to write runtime-injected asm\n");
        return -1;
    }
    close(fd);
    return 0;
}

void _start(int argc, char **argv) {
    const char *input = 0;
    const char *output = 0;
    int keep_temps = 0;
    char out_buf[160];
    char pid_buf[16];
    char asm_tmp[64];
    char obj_tmp[64];

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
    memset(asm_tmp, 0, sizeof(asm_tmp));
    memset(obj_tmp, 0, sizeof(obj_tmp));
    append_str(asm_tmp, sizeof(asm_tmp), "cc_");
    append_str(asm_tmp, sizeof(asm_tmp), pid_buf);
    append_str(asm_tmp, sizeof(asm_tmp), ".asm");
    append_str(obj_tmp, sizeof(obj_tmp), "cc_");
    append_str(obj_tmp, sizeof(obj_tmp), pid_buf);
    append_str(obj_tmp, sizeof(obj_tmp), ".obj");

    cleanup_stale_cc_temps();

    {
        const char *a1[] = {
            "smallerc.elf",
            "-seg32",
            "-no-leading-underscore",
            input,
            asm_tmp,
            0
        };
        if (run_stage("smallerc.elf", a1, 5) != 0) {
            if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
            exit(1);
        }
        if (require_nonempty_file(asm_tmp, "smallerc") != 0) {
            if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
            exit(1);
        }
        if (inject_runtime_asm(asm_tmp) != 0) {
            if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
            exit(1);
        }
    }
    {
        const char *a2[] = { "as86.elf", "-f", "obj", "--org", "0x700000", "-o", obj_tmp, asm_tmp, 0 };
        if (run_stage("as86.elf", a2, 8) != 0) {
            if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
            exit(1);
        }
        if (require_nonempty_file(obj_tmp, "as86") != 0) {
            if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
            exit(1);
        }
    }
    {
        const char *a3[] = { "ld86.elf", "-o", output, obj_tmp, 0 };
        if (run_stage("ld86.elf", a3, 4) != 0) {
            if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
            exit(1);
        }
    }

    if (!keep_temps) cleanup_tmp_pair(asm_tmp, obj_tmp);
    else {
        print("cc: temp files: ");
        print(asm_tmp);
        print(" ");
        print(obj_tmp);
        print("\n");
    }

    print("cc: built ");
    print(output);
    print("\n");
    exit(0);
}
