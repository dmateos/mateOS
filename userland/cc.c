#include "syscalls.h"
#include "libc.h"

static void usage(void) {
    print("usage: cc <input.c> [-o output.elf]\n");
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

static void fail_keep_tmps(const char *asm_tmp, const char *bin_tmp) {
    print("cc: keeping temp files for debug: ");
    print(asm_tmp);
    print(" ");
    print(bin_tmp);
    print("\n");
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
        return -1;
    }
    src[st.size] = '\0';
    int n2 = (int)st.size;

    // Reorder SmallerC output so executable .text is contiguous.
    // as86 phase-1 is flat and doesn't implement true section layout yet.
    char *text = (char *)malloc(st.size + 1);
    char *rodata = (char *)malloc(st.size + 1);
    char *data = (char *)malloc(st.size + 1);
    char *bss = (char *)malloc(st.size + 1);
    if (!text || !rodata || !data || !bss) {
        print("cc: out of memory in asm section reorder\n");
        return -1;
    }
    text[0] = rodata[0] = data[0] = bss[0] = '\0';
    int tlen = 0, rlen = 0, dlen = 0, blen = 0;

    enum { SEC_TEXT, SEC_RODATA, SEC_DATA, SEC_BSS } cur = SEC_TEXT;
    int i = 0;
    while (i < n2) {
        int j = i;
        while (j < n2 && src[j] != '\n') j++;
        int ll = j - i;
        const char *ln = src + i;

        // Trim-left for section detection
        int k = 0;
        while (k < ll && (ln[k] == ' ' || ln[k] == '\t' || ln[k] == '\r')) k++;
        if (k + 8 <= ll &&
            ln[k + 0] == 's' && ln[k + 1] == 'e' && ln[k + 2] == 'c' && ln[k + 3] == 't' &&
            ln[k + 4] == 'i' && ln[k + 5] == 'o' && ln[k + 6] == 'n' &&
            (ln[k + 7] == ' ' || ln[k + 7] == '\t')) {
            const char *p = ln + k + 8;
            int rem = ll - (k + 8);
            while (rem > 0 && (*p == ' ' || *p == '\t')) { p++; rem--; }
            if (rem >= 5 && p[0] == '.' && p[1] == 't' && p[2] == 'e' && p[3] == 'x' && p[4] == 't') cur = SEC_TEXT;
            else if (rem >= 7 && p[0] == '.' && p[1] == 'r' && p[2] == 'o' && p[3] == 'd' && p[4] == 'a' && p[5] == 't' && p[6] == 'a') cur = SEC_RODATA;
            else if (rem >= 5 && p[0] == '.' && p[1] == 'd' && p[2] == 'a' && p[3] == 't' && p[4] == 'a') cur = SEC_DATA;
            else if (rem >= 4 && p[0] == '.' && p[1] == 'b' && p[2] == 's' && p[3] == 's') cur = SEC_BSS;
            i = (j < n2) ? (j + 1) : j;
            continue;
        }

        // Copy line + '\n' to current section buffer
        char *dst = text;
        int *dl = &tlen;
        if (cur == SEC_RODATA) { dst = rodata; dl = &rlen; }
        else if (cur == SEC_DATA) { dst = data; dl = &dlen; }
        else if (cur == SEC_BSS) { dst = bss; dl = &blen; }

        memcpy(dst + *dl, ln, ll);
        *dl += ll;
        dst[*dl] = '\n';
        (*dl)++;
        dst[*dl] = '\0';

        i = (j < n2) ? (j + 1) : j;
    }

    fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        print("cc: failed to reopen asm for runtime inject\n");
        return -1;
    }

    int n1 = strlen(rt_prefix);
    int n3 = strlen(rt_suffix);
    const char *sec_text = "section .text\n";
    const char *sec_rodata = "section .rodata\n";
    const char *sec_data = "section .data\n";
    const char *sec_bss = "section .bss\n";
    int ok = 1;
    if (fwrite(fd, rt_prefix, (unsigned int)n1) != n1) ok = 0;
    if (ok && fwrite(fd, sec_text, (unsigned int)strlen(sec_text)) != strlen(sec_text)) ok = 0;
    if (ok && tlen && fwrite(fd, text, (unsigned int)tlen) != tlen) ok = 0;
    if (ok && fwrite(fd, rt_suffix, (unsigned int)n3) != n3) ok = 0;
    if (ok && rlen) {
        if (fwrite(fd, sec_rodata, (unsigned int)strlen(sec_rodata)) != strlen(sec_rodata)) ok = 0;
        else if (fwrite(fd, rodata, (unsigned int)rlen) != rlen) ok = 0;
    }
    if (ok && dlen) {
        if (fwrite(fd, sec_data, (unsigned int)strlen(sec_data)) != strlen(sec_data)) ok = 0;
        else if (fwrite(fd, data, (unsigned int)dlen) != dlen) ok = 0;
    }
    if (ok && blen) {
        if (fwrite(fd, sec_bss, (unsigned int)strlen(sec_bss)) != strlen(sec_bss)) ok = 0;
        else if (fwrite(fd, bss, (unsigned int)blen) != blen) ok = 0;
    }
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
    char out_buf[160];
    char pid_buf[16];
    char asm_tmp[64];
    char bin_tmp[64];

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
    memset(bin_tmp, 0, sizeof(bin_tmp));
    append_str(asm_tmp, sizeof(asm_tmp), "cc_");
    append_str(asm_tmp, sizeof(asm_tmp), pid_buf);
    append_str(asm_tmp, sizeof(asm_tmp), ".asm");
    append_str(bin_tmp, sizeof(bin_tmp), "cc_");
    append_str(bin_tmp, sizeof(bin_tmp), pid_buf);
    append_str(bin_tmp, sizeof(bin_tmp), ".bin");

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
            fail_keep_tmps(asm_tmp, bin_tmp);
            exit(1);
        }
        if (require_nonempty_file(asm_tmp, "smallerc") != 0) {
            fail_keep_tmps(asm_tmp, bin_tmp);
            exit(1);
        }
        if (inject_runtime_asm(asm_tmp) != 0) {
            fail_keep_tmps(asm_tmp, bin_tmp);
            exit(1);
        }
    }
    {
        const char *a2[] = { "as86.elf", "-f", "bin", "--org", "0x700000", "-o", bin_tmp, asm_tmp, 0 };
        if (run_stage("as86.elf", a2, 8) != 0) {
            fail_keep_tmps(asm_tmp, bin_tmp);
            exit(1);
        }
        if (require_nonempty_file(bin_tmp, "as86") != 0) {
            fail_keep_tmps(asm_tmp, bin_tmp);
            exit(1);
        }
    }
    {
        const char *a3[] = { "ld86.elf", "-o", output, bin_tmp, 0 };
        if (run_stage("ld86.elf", a3, 4) != 0) {
            fail_keep_tmps(asm_tmp, bin_tmp);
            exit(1);
        }
    }

    // Keep temp files for now to make pipeline debugging easier.
    print("cc: temp files: ");
    print(asm_tmp);
    print(" ");
    print(bin_tmp);
    print("\n");

    print("cc: built ");
    print(output);
    print("\n");
    exit(0);
}
