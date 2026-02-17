#include "syscalls.h"
#include "libc.h"

#define MAX_IN (2 * 1024 * 1024)

typedef struct __attribute__((packed)) {
    unsigned char e_ident[16];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int e_version;
    unsigned int e_entry;
    unsigned int e_phoff;
    unsigned int e_shoff;
    unsigned int e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} elf32_ehdr_t;

typedef struct __attribute__((packed)) {
    unsigned int p_type;
    unsigned int p_offset;
    unsigned int p_vaddr;
    unsigned int p_paddr;
    unsigned int p_filesz;
    unsigned int p_memsz;
    unsigned int p_flags;
    unsigned int p_align;
} elf32_phdr_t;

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int parse_int_local(const char *s, int *out) {
    int sign = 1;
    int i = 0;
    int base = 10;
    int v = 0;
    if (s[0] == '-') { sign = -1; i++; }
    else if (s[0] == '+') { i++; }
    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }
    if (!s[i]) return 0;
    for (; s[i]; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else return 0;
        if (d >= base) return 0;
        v = v * base + d;
    }
    *out = v * sign;
    return 1;
}

static unsigned int align_up(unsigned int v, unsigned int a) {
    return (v + a - 1u) & ~(a - 1u);
}

static void usage(void) {
    print("usage: ld86 [-base addr] [-entry addr] [-o out.elf] <input.bin> [output.elf]\n");
    print("phase-1: flat-binary to ELF32 packer (single PT_LOAD)\n");
}

void _start(int argc, char **argv) {
    const char *in = 0;
    const char *out = 0;
    unsigned int base = 0x700000;
    unsigned int entry = 0x700000;
    int entry_set = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (streq(a, "-o")) {
            if (i + 1 >= argc) { usage(); exit(1); }
            out = argv[++i];
            continue;
        }
        if (streq(a, "-base") || streq(a, "--base")) {
            int v = 0;
            if (i + 1 >= argc || !parse_int_local(argv[++i], &v) || v < 0) {
                print("ld86: bad base value\n");
                exit(1);
            }
            base = (unsigned int)v;
            if (!entry_set) entry = base;
            continue;
        }
        if (streq(a, "-entry") || streq(a, "--entry")) {
            int v = 0;
            if (i + 1 >= argc || !parse_int_local(argv[++i], &v) || v < 0) {
                print("ld86: bad entry value\n");
                exit(1);
            }
            entry = (unsigned int)v;
            entry_set = 1;
            continue;
        }
        if (a[0] == '-') {
            print("ld86: unknown option: ");
            print(a);
            print("\n");
            exit(1);
        }
        if (!in) in = a;
        else if (!out) out = a;
        else {
            usage();
            exit(1);
        }
    }

    if (!in || !out) {
        usage();
        exit(1);
    }

    stat_t st;
    if (stat(in, &st) < 0 || st.size == 0 || st.size > MAX_IN) {
        print("ld86: bad input size\n");
        exit(1);
    }

    int ifd = open(in, O_RDONLY);
    if (ifd < 0) {
        print("ld86: cannot open input\n");
        exit(1);
    }
    unsigned char *ibuf = (unsigned char *)malloc(st.size);
    if (!ibuf) {
        close(ifd);
        print("ld86: out of memory\n");
        exit(1);
    }
    int rn = fread(ifd, ibuf, st.size);
    close(ifd);
    if (rn != (int)st.size) {
        print("ld86: read failed\n");
        exit(1);
    }

    const unsigned int page = 0x1000;
    const unsigned int phoff = sizeof(elf32_ehdr_t);
    const unsigned int code_off = align_up(sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t), page);
    const unsigned int out_sz = code_off + st.size;

    unsigned char *obuf = (unsigned char *)malloc(out_sz);
    if (!obuf) {
        print("ld86: out of memory\n");
        exit(1);
    }
    memset(obuf, 0, out_sz);

    elf32_ehdr_t *eh = (elf32_ehdr_t *)obuf;
    elf32_phdr_t *ph = (elf32_phdr_t *)(obuf + phoff);

    eh->e_ident[0] = 0x7F;
    eh->e_ident[1] = 'E';
    eh->e_ident[2] = 'L';
    eh->e_ident[3] = 'F';
    eh->e_ident[4] = 1;  // ELFCLASS32
    eh->e_ident[5] = 1;  // ELFDATA2LSB
    eh->e_ident[6] = 1;  // EV_CURRENT
    eh->e_type = 2;      // ET_EXEC
    eh->e_machine = 3;   // EM_386
    eh->e_version = 1;
    eh->e_entry = entry;
    eh->e_phoff = phoff;
    eh->e_shoff = 0;
    eh->e_flags = 0;
    eh->e_ehsize = sizeof(elf32_ehdr_t);
    eh->e_phentsize = sizeof(elf32_phdr_t);
    eh->e_phnum = 1;
    eh->e_shentsize = 0;
    eh->e_shnum = 0;
    eh->e_shstrndx = 0;

    ph->p_type = 1;      // PT_LOAD
    ph->p_offset = code_off;
    ph->p_vaddr = base;
    ph->p_paddr = base;
    ph->p_filesz = st.size;
    ph->p_memsz = st.size;
    ph->p_flags = 7;     // RWX
    ph->p_align = page;

    memcpy(obuf + code_off, ibuf, st.size);

    int ofd = open(out, O_WRONLY | O_CREAT | O_TRUNC);
    if (ofd < 0) {
        print("ld86: cannot open output\n");
        exit(1);
    }
    if (fwrite(ofd, obuf, out_sz) != (int)out_sz) {
        close(ofd);
        print("ld86: write failed\n");
        exit(1);
    }
    close(ofd);

    print("ld86: wrote ");
    print_num((int)out_sz);
    print(" bytes to ");
    print(out);
    print("\n");
    exit(0);
}
