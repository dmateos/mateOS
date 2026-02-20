#include "syscalls.h"
#include "libc.h"

#define MAX_IN_BYTES (2 * 1024 * 1024)
#define MAX_INPUTS 32

#define MOBJ_SYM_GLOBAL 0x1u

#define MOBJ_RELOC_ABS32 1u
#define MOBJ_RELOC_REL32 2u

#define SEC_TEXT 0u
#define SEC_RODATA 1u
#define SEC_DATA 2u
#define SEC_BSS 3u
#define SEC_UNDEF 0xFFFFFFFFu

typedef struct __attribute__((packed)) {
    unsigned char magic[4];   // "MOBJ"
    unsigned int version;     // 1
    unsigned int org;
    unsigned int entry_off;
    unsigned int text_size;
    unsigned int rodata_size;
    unsigned int data_size;
    unsigned int bss_size;
} mobj_header_v1_t;

typedef struct __attribute__((packed)) {
    unsigned char magic[4];   // "MOBJ"
    unsigned int version;     // 2
    unsigned int org;
    unsigned int entry_off;
    unsigned int text_size;
    unsigned int rodata_size;
    unsigned int data_size;
    unsigned int bss_size;
    unsigned int sym_count;
    unsigned int reloc_count;
} mobj_header_v2_t;

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

typedef struct {
    const char *path;
    unsigned char *buf;
    unsigned int size;

    int is_obj;
    unsigned int obj_version;
    unsigned int entry_off;

    unsigned char *payload;
    unsigned int payload_size;     // text+rodata+data
    unsigned int bss_size;

    unsigned int sec_base[4];      // local section offsets in payload

    mobj_sym_t *syms;
    unsigned int sym_count;
    mobj_reloc_t *rels;
    unsigned int rel_count;

    unsigned int image_off;        // offset in final load image
} input_t;

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int parse_int_local(const char *s, int *out) {
    int sign = 1, i = 0, base = 10, v = 0;
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

static int is_mobj_magic(const unsigned char *b, unsigned int n) {
    return n >= 8 && b[0] == 'M' && b[1] == 'O' && b[2] == 'B' && b[3] == 'J';
}

static void usage(void) {
    print("usage: ld86 [-base addr] [-entry addr] [-o out.elf] <in1.obj|bin> [in2.obj|bin ...] [out.elf]\n");
    print("phase-2: object/binary linker to ELF32 (single PT_LOAD)\n");
}

static unsigned char *read_whole_file(const char *path, unsigned int *out_size) {
    stat_t st;
    if (stat(path, &st) < 0 || st.size == 0 || st.size > MAX_IN_BYTES) return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    unsigned char *buf = (unsigned char *)malloc(st.size);
    if (!buf) {
        close(fd);
        return 0;
    }
    int rn = fread(fd, buf, st.size);
    close(fd);
    if (rn != (int)st.size) return 0;
    *out_size = st.size;
    return buf;
}

static int parse_input(input_t *in) {
    memset(in->sec_base, 0, sizeof(in->sec_base));
    in->is_obj = 0;
    in->obj_version = 0;
    in->entry_off = 0;
    in->syms = 0;
    in->sym_count = 0;
    in->rels = 0;
    in->rel_count = 0;
    in->bss_size = 0;

    if (!is_mobj_magic(in->buf, in->size)) {
        in->payload = in->buf;
        in->payload_size = in->size;
        return 1;
    }

    unsigned int ver = *(unsigned int *)(in->buf + 4);
    if (ver == 1) {
        if (in->size < sizeof(mobj_header_v1_t)) {
            print("ld86: truncated v1 object: ");
            print(in->path);
            print("\n");
            return 0;
        }
        mobj_header_v1_t *h = (mobj_header_v1_t *)in->buf;
        unsigned int filesz = h->text_size + h->rodata_size + h->data_size;
        unsigned int need = (unsigned int)sizeof(mobj_header_v1_t) + filesz;
        if (need > in->size) {
            print("ld86: bad v1 object size: ");
            print(in->path);
            print("\n");
            return 0;
        }
        in->is_obj = 1;
        in->obj_version = 1;
        in->entry_off = h->entry_off;
        in->payload = in->buf + sizeof(mobj_header_v1_t);
        in->payload_size = filesz;
        in->bss_size = h->bss_size;
        in->sec_base[SEC_TEXT] = 0;
        in->sec_base[SEC_RODATA] = h->text_size;
        in->sec_base[SEC_DATA] = h->text_size + h->rodata_size;
        in->sec_base[SEC_BSS] = h->text_size + h->rodata_size + h->data_size;
        return 1;
    }

    if (ver == 2) {
        if (in->size < sizeof(mobj_header_v2_t)) {
            print("ld86: truncated v2 object: ");
            print(in->path);
            print("\n");
            return 0;
        }
        mobj_header_v2_t *h = (mobj_header_v2_t *)in->buf;
        unsigned int filesz = h->text_size + h->rodata_size + h->data_size;
        unsigned int sym_bytes = h->sym_count * (unsigned int)sizeof(mobj_sym_t);
        unsigned int rel_bytes = h->reloc_count * (unsigned int)sizeof(mobj_reloc_t);
        if (h->sym_count && sym_bytes / (unsigned int)sizeof(mobj_sym_t) != h->sym_count) return 0;
        if (h->reloc_count && rel_bytes / (unsigned int)sizeof(mobj_reloc_t) != h->reloc_count) return 0;
        unsigned int need = (unsigned int)sizeof(mobj_header_v2_t) + filesz + sym_bytes + rel_bytes;
        if (need > in->size) {
            print("ld86: bad v2 object size: ");
            print(in->path);
            print("\n");
            return 0;
        }
        in->is_obj = 1;
        in->obj_version = 2;
        in->entry_off = h->entry_off;
        in->payload = in->buf + sizeof(mobj_header_v2_t);
        in->payload_size = filesz;
        in->bss_size = h->bss_size;
        in->sec_base[SEC_TEXT] = 0;
        in->sec_base[SEC_RODATA] = h->text_size;
        in->sec_base[SEC_DATA] = h->text_size + h->rodata_size;
        in->sec_base[SEC_BSS] = h->text_size + h->rodata_size + h->data_size;
        in->sym_count = h->sym_count;
        in->rel_count = h->reloc_count;
        in->syms = (mobj_sym_t *)(in->payload + filesz);
        in->rels = (mobj_reloc_t *)((unsigned char *)in->syms + sym_bytes);
        return 1;
    }

    print("ld86: unsupported object version in ");
    print(in->path);
    print("\n");
    return 0;
}

static int resolve_symbol_addr(input_t *inputs, int input_count,
                               int owner_idx, unsigned int sym_idx,
                               unsigned int base, unsigned int *out_addr) {
    input_t *owner = &inputs[owner_idx];
    if (sym_idx >= owner->sym_count) return 0;
    mobj_sym_t *s = &owner->syms[sym_idx];

    if (s->section != SEC_UNDEF) {
        if (s->section > SEC_BSS) return 0;
        *out_addr = base + owner->image_off + owner->sec_base[s->section] + s->value_off;
        return 1;
    }

    int found = 0;
    unsigned int found_addr = 0;
    for (int i = 0; i < input_count; i++) {
        input_t *cand = &inputs[i];
        if (!cand->is_obj || cand->obj_version < 2) continue;
        for (unsigned int j = 0; j < cand->sym_count; j++) {
            mobj_sym_t *cs = &cand->syms[j];
            if (cs->section == SEC_UNDEF) continue;
            if (!(cs->flags & MOBJ_SYM_GLOBAL)) continue;
            if (strcmp(cs->name, s->name) != 0) continue;
            unsigned int addr = base + cand->image_off + cand->sec_base[cs->section] + cs->value_off;
            if (found && found_addr != addr) {
                print("ld86: duplicate global symbol: ");
                print(s->name);
                print("\n");
                return 0;
            }
            found = 1;
            found_addr = addr;
        }
    }
    if (!found) {
        print("ld86: undefined symbol: ");
        print(s->name);
        print("\n");
        return 0;
    }
    *out_addr = found_addr;
    return 1;
}

void _start(int argc, char **argv) {
    const char *out = 0;
    const char *pos[MAX_INPUTS];
    int pos_count = 0;
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
        if (pos_count >= MAX_INPUTS) {
            print("ld86: too many input files\n");
            exit(1);
        }
        pos[pos_count++] = a;
    }

    if (!out) {
        if (pos_count == 2) {
            out = pos[1];
            pos_count = 1;
        } else {
            usage();
            exit(1);
        }
    }
    if (pos_count <= 0) {
        usage();
        exit(1);
    }

    input_t inputs[MAX_INPUTS];
    memset(inputs, 0, sizeof(inputs));

    unsigned int total_file = 0;
    unsigned int total_mem = 0;
    int chosen_entry = 0;

    for (int i = 0; i < pos_count; i++) {
        inputs[i].path = pos[i];
        inputs[i].buf = read_whole_file(pos[i], &inputs[i].size);
        if (!inputs[i].buf) {
            print("ld86: cannot read input: ");
            print(pos[i]);
            print("\n");
            exit(1);
        }
        if (!parse_input(&inputs[i])) exit(1);

        inputs[i].image_off = total_file;
        total_file += inputs[i].payload_size;
        total_mem += inputs[i].payload_size + inputs[i].bss_size;

        if (!entry_set && !chosen_entry && inputs[i].is_obj) {
            entry = base + inputs[i].image_off + inputs[i].entry_off;
            chosen_entry = 1;
        }
    }

    unsigned char *image = (unsigned char *)malloc(total_file > 0 ? total_file : 1);
    if (!image) {
        print("ld86: out of memory\n");
        exit(1);
    }
    if (total_file) memset(image, 0, total_file);

    for (int i = 0; i < pos_count; i++) {
        if (inputs[i].payload_size > 0) {
            memcpy(image + inputs[i].image_off, inputs[i].payload, inputs[i].payload_size);
        }
    }

    for (int i = 0; i < pos_count; i++) {
        input_t *in = &inputs[i];
        if (!in->is_obj || in->obj_version < 2 || in->rel_count == 0) continue;

        for (unsigned int rix = 0; rix < in->rel_count; rix++) {
            mobj_reloc_t *r = &in->rels[rix];
            if (r->section > SEC_DATA) {
                print("ld86: bad reloc section\n");
                exit(1);
            }
            unsigned int place_local = in->sec_base[r->section] + r->offset;
            if (place_local + 4 > in->payload_size) {
                print("ld86: reloc out of range\n");
                exit(1);
            }
            unsigned int place_off = in->image_off + place_local;

            unsigned int sym_addr = 0;
            if (!resolve_symbol_addr(inputs, pos_count, i, r->sym_index, base, &sym_addr)) {
                exit(1);
            }

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
            wr32(image + place_off, (unsigned int)value);
        }
    }

    const unsigned int page = 0x1000;
    const unsigned int phoff = sizeof(elf32_ehdr_t);
    const unsigned int code_off = align_up(sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t), page);
    const unsigned int out_sz = code_off + total_file;

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
    eh->e_ident[4] = 1;
    eh->e_ident[5] = 1;
    eh->e_ident[6] = 1;
    eh->e_type = 2;
    eh->e_machine = 3;
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

    ph->p_type = 1;
    ph->p_offset = code_off;
    ph->p_vaddr = base;
    ph->p_paddr = base;
    ph->p_filesz = total_file;
    ph->p_memsz = total_mem;
    ph->p_flags = 7;
    ph->p_align = page;

    if (total_file) memcpy(obuf + code_off, image, total_file);

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
