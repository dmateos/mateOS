#include "syscalls.h"
#include "libc.h"

#define MAX_IN (2 * 1024 * 1024)

#define MOBJ_SYM_GLOBAL 0x1u
#define MOBJ_SYM_EXTERN 0x2u

#define MOBJ_RELOC_ABS32 1u
#define MOBJ_RELOC_REL32 2u

#define SEC_TEXT 0u
#define SEC_RODATA 1u
#define SEC_DATA 2u
#define SEC_BSS 3u
#define SEC_UNDEF 0xFFFFFFFFu

typedef struct __attribute__((packed)) {
    unsigned char magic[4];   // "MOBJ"
    unsigned int version;     // 1 or 2
    unsigned int org;
    unsigned int entry_off;   // offset from start of image
    unsigned int text_size;
    unsigned int rodata_size;
    unsigned int data_size;
    unsigned int bss_size;
    unsigned int sym_count;   // v2+
    unsigned int reloc_count; // v2+
} mobj_header_t;

typedef struct __attribute__((packed)) {
    char name[64];
    unsigned int value_off;
    unsigned int section; // SEC_*, or SEC_UNDEF
    unsigned int flags;   // MOBJ_SYM_*
} mobj_sym_t;

typedef struct __attribute__((packed)) {
    unsigned int section; // SEC_*
    unsigned int offset;
    unsigned int type;    // MOBJ_RELOC_*
    unsigned int sym_index;
    int addend;
} mobj_reloc_t;

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

static void wr32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void usage(void) {
    print("usage: ld86 [-base addr] [-entry addr] [-o out.elf] <input.bin|input.obj> [output.elf]\n");
    print("phase-1: flat-binary/object to ELF32 packer (single PT_LOAD)\n");
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
    unsigned int file_sz = st.size;
    unsigned int mem_sz = st.size;
    unsigned char *payload = ibuf;
    int payload_sz = st.size;
    mobj_header_t *mh = 0;
    mobj_sym_t *msyms = 0;
    mobj_reloc_t *mrels = 0;
    unsigned int sec_base[4] = {0,0,0,0};

    if (st.size >= (int)sizeof(mobj_header_t)) {
        mh = (mobj_header_t *)ibuf;
        if (mh->magic[0] == 'M' && mh->magic[1] == 'O' && mh->magic[2] == 'B' && mh->magic[3] == 'J') {
            if (mh->version != 1 && mh->version != 2) {
                print("ld86: unsupported object version\n");
                exit(1);
            }
            unsigned int seg_filesz = mh->text_size + mh->rodata_size + mh->data_size;
            unsigned int need = (unsigned int)sizeof(mobj_header_t) + seg_filesz;
            if (mh->version >= 2) {
                unsigned int sym_bytes = mh->sym_count * (unsigned int)sizeof(mobj_sym_t);
                unsigned int rel_bytes = mh->reloc_count * (unsigned int)sizeof(mobj_reloc_t);
                if (sym_bytes / sizeof(mobj_sym_t) != mh->sym_count || rel_bytes / sizeof(mobj_reloc_t) != mh->reloc_count) {
                    print("ld86: bad object table sizes\n");
                    exit(1);
                }
                need += sym_bytes + rel_bytes;
            }
            if (need > st.size) {
                print("ld86: bad object size\n");
                exit(1);
            }
            payload = ibuf + sizeof(mobj_header_t);
            payload_sz = (int)seg_filesz;
            file_sz = seg_filesz;
            mem_sz = seg_filesz + mh->bss_size;
            if (!entry_set) entry = base + mh->entry_off;

            sec_base[SEC_TEXT] = 0;
            sec_base[SEC_RODATA] = mh->text_size;
            sec_base[SEC_DATA] = mh->text_size + mh->rodata_size;
            sec_base[SEC_BSS] = mh->text_size + mh->rodata_size + mh->data_size;

            if (mh->version >= 2) {
                msyms = (mobj_sym_t *)(payload + seg_filesz);
                mrels = (mobj_reloc_t *)((unsigned char *)msyms + (mh->sym_count * (unsigned int)sizeof(mobj_sym_t)));
            }
        }
    }

    const unsigned int phoff = sizeof(elf32_ehdr_t);
    const unsigned int code_off = align_up(sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t), page);
    const unsigned int out_sz = code_off + file_sz;

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
    ph->p_filesz = file_sz;
    ph->p_memsz = mem_sz;
    ph->p_flags = 7;     // RWX
    ph->p_align = page;

    memcpy(obuf + code_off, payload, payload_sz);

    if (mh && mh->version >= 2 && mh->reloc_count > 0) {
        unsigned char *img = obuf + code_off;
        for (unsigned int i = 0; i < mh->reloc_count; i++) {
            mobj_reloc_t *r = &mrels[i];
            if (r->section > SEC_DATA) {
                print("ld86: bad reloc section\n");
                exit(1);
            }
            if (r->sym_index >= mh->sym_count) {
                print("ld86: bad reloc symbol index\n");
                exit(1);
            }
            mobj_sym_t *s = &msyms[r->sym_index];
            if (s->section == SEC_UNDEF) {
                print("ld86: undefined symbol: ");
                print(s->name);
                print("\n");
                exit(1);
            }
            if (s->section > SEC_BSS) {
                print("ld86: bad symbol section\n");
                exit(1);
            }

            unsigned int place_off = sec_base[r->section] + r->offset;
            if (place_off + 4 > file_sz) {
                print("ld86: reloc out of range\n");
                exit(1);
            }
            unsigned int sym_addr = base + sec_base[s->section] + s->value_off;
            int value = 0;
            if (r->type == MOBJ_RELOC_ABS32) {
                value = (int)sym_addr + r->addend;
            } else if (r->type == MOBJ_RELOC_REL32) {
                unsigned int place_addr = base + place_off;
                value = (int)sym_addr + r->addend - (int)(place_addr + 4);
            } else {
                print("ld86: unknown relocation type\n");
                exit(1);
            }
            wr32(img + place_off, (unsigned int)value);
        }
    }

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
