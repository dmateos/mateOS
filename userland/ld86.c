#include "syscalls.h"
#include "libc.h"

#define MAX_IN_BYTES (2 * 1024 * 1024)
#define MAX_INPUTS 128

#define MOBJ_SYM_GLOBAL 0x1u

#define MOBJ_RELOC_ABS32 1u
#define MOBJ_RELOC_REL32 2u

#define ELF_MAGIC0 0x7Fu
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define ELF_ET_REL 1u
#define ELF_EM_386 3u

#define ELF_SHT_NULL 0u
#define ELF_SHT_PROGBITS 1u
#define ELF_SHT_SYMTAB 2u
#define ELF_SHT_STRTAB 3u
#define ELF_SHT_REL 9u
#define ELF_SHT_NOBITS 8u

#define ELF_SHF_WRITE 0x1u
#define ELF_SHF_ALLOC 0x2u
#define ELF_SHF_EXECINSTR 0x4u

#define ELF_SHN_UNDEF 0u
#define ELF_SHN_ABS 0xFFF1u

#define ELF_STB_LOCAL 0u
#define ELF_STB_GLOBAL 1u
#define ELF_STB_WEAK 2u

#define ELF_R_386_32 1u
#define ELF_R_386_PC32 2u

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

typedef struct __attribute__((packed)) {
    unsigned int sh_name;
    unsigned int sh_type;
    unsigned int sh_flags;
    unsigned int sh_addr;
    unsigned int sh_offset;
    unsigned int sh_size;
    unsigned int sh_link;
    unsigned int sh_info;
    unsigned int sh_addralign;
    unsigned int sh_entsize;
} elf32_shdr_t;

typedef struct __attribute__((packed)) {
    unsigned int st_name;
    unsigned int st_value;
    unsigned int st_size;
    unsigned char st_info;
    unsigned char st_other;
    unsigned short st_shndx;
} elf32_sym_t;

typedef struct __attribute__((packed)) {
    unsigned int r_offset;
    unsigned int r_info;
} elf32_rel_t;

typedef struct __attribute__((packed)) {
    char name[16];
    char mtime[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char fmag[2];
} ar_hdr_t;

typedef struct {
    const char *path;
    char path_buf[96];
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

static int sym_name_eq_loose(const char *a, const char *b) {
    if (!a || !b) return 0;
    if (strcmp(a, b) == 0) return 1;
    if (a[0] == '$' && strcmp(a + 1, b) == 0) return 1;
    if (b[0] == '$' && strcmp(a, b + 1) == 0) return 1;
    if (a[0] == '$' && b[0] == '$' && strcmp(a + 1, b + 1) == 0) return 1;
    return 0;
}

static const char *sym_strip_dollar(const char *s) {
    return (s && s[0] == '$') ? (s + 1) : s;
}

static void path_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void path_append(char *dst, int cap, const char *src) {
    int n = strlen(dst);
    int i = 0;
    while (src[i] && n + i + 1 < cap) {
        dst[n + i] = src[i];
        i++;
    }
    dst[n + i] = 0;
}

static void ar_member_name(const ar_hdr_t *ah, char *out, int out_cap) {
    int j = 0;
    for (int i = 0; i < 16 && j + 1 < out_cap; i++) {
        char c = ah->name[i];
        if (c == '/' || c == ' ' || c == '\0') break;
        out[j++] = c;
    }
    if (j == 0 && out_cap >= 2) {
        out[0] = '?';
        j = 1;
    }
    out[j] = 0;
}

static void set_input_path(input_t *in, const char *file, const char *member) {
    memset(in->path_buf, 0, sizeof(in->path_buf));
    path_copy(in->path_buf, sizeof(in->path_buf), file);
    if (member && member[0]) {
        path_append(in->path_buf, sizeof(in->path_buf), ":");
        path_append(in->path_buf, sizeof(in->path_buf), member);
    }
    in->path = in->path_buf;
}

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

static unsigned int rd32(const unsigned char *p) {
    return (unsigned int)p[0]
        | ((unsigned int)p[1] << 8)
        | ((unsigned int)p[2] << 16)
        | ((unsigned int)p[3] << 24);
}

static int parse_u32_dec_field(const char *s, int n, unsigned int *out) {
    unsigned int v = 0;
    int seen = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t') continue;
        if (c < '0' || c > '9') return 0;
        seen = 1;
        v = v * 10u + (unsigned int)(c - '0');
    }
    if (!seen) return 0;
    *out = v;
    return 1;
}

static int is_mobj_magic(const unsigned char *b, unsigned int n) {
    return n >= 8 && b[0] == 'M' && b[1] == 'O' && b[2] == 'B' && b[3] == 'J';
}

static int is_elf_rel_object(const unsigned char *b, unsigned int n) {
    if (n < sizeof(elf32_ehdr_t)) return 0;
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)b;
    if (eh->e_ident[0] != ELF_MAGIC0 || eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 || eh->e_ident[3] != ELF_MAGIC3) return 0;
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB) return 0;
    if (eh->e_type != ELF_ET_REL || eh->e_machine != ELF_EM_386) return 0;
    return 1;
}

static int is_ar_archive(const unsigned char *b, unsigned int n) {
    static const char sig[8] = { '!', '<', 'a', 'r', 'c', 'h', '>', '\n' };
    if (n < 8) return 0;
    for (int i = 0; i < 8; i++) {
        if ((char)b[i] != sig[i]) return 0;
    }
    return 1;
}

static int sec_kind_from_elf(const elf32_shdr_t *sh) {
    if (!(sh->sh_flags & ELF_SHF_ALLOC)) return -1;
    if (sh->sh_type == ELF_SHT_NOBITS) return (int)SEC_BSS;
    if (sh->sh_flags & ELF_SHF_EXECINSTR) return (int)SEC_TEXT;
    if (sh->sh_flags & ELF_SHF_WRITE) return (int)SEC_DATA;
    return (int)SEC_RODATA;
}

static int parse_elf_rel_input(input_t *in) {
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)in->buf;
    if (eh->e_shentsize != sizeof(elf32_shdr_t) || eh->e_shnum == 0) {
        print("ld86: bad ELF section table: ");
        print(in->path);
        print("\n");
        return 0;
    }
    unsigned int sht_end = eh->e_shoff + (unsigned int)eh->e_shnum * (unsigned int)sizeof(elf32_shdr_t);
    if (eh->e_shoff >= in->size || sht_end > in->size || sht_end < eh->e_shoff) {
        print("ld86: truncated ELF section table: ");
        print(in->path);
        print("\n");
        return 0;
    }

    const elf32_shdr_t *shdrs = (const elf32_shdr_t *)(in->buf + eh->e_shoff);
    unsigned int shnum = eh->e_shnum;
    int *sh_kind = (int *)malloc(shnum * sizeof(int));
    unsigned int *sh_off_in_kind = (unsigned int *)malloc(shnum * sizeof(unsigned int));
    if (!sh_kind || !sh_off_in_kind) {
        print("ld86: out of memory\n");
        return 0;
    }
    for (unsigned int i = 0; i < shnum; i++) {
        sh_kind[i] = -1;
        sh_off_in_kind[i] = 0;
    }

    unsigned int kind_size[4];
    memset(kind_size, 0, sizeof(kind_size));

    for (unsigned int i = 0; i < shnum; i++) {
        const elf32_shdr_t *sh = &shdrs[i];
        int kind = sec_kind_from_elf(sh);
        if (kind < 0) continue;
        unsigned int al = sh->sh_addralign ? sh->sh_addralign : 1u;
        kind_size[kind] = align_up(kind_size[kind], al);
        sh_off_in_kind[i] = kind_size[kind];
        kind_size[kind] += sh->sh_size;
        sh_kind[i] = kind;
    }

    in->is_obj = 1;
    in->obj_version = 2;
    in->entry_off = 0;
    in->sec_base[SEC_TEXT] = 0;
    in->sec_base[SEC_RODATA] = kind_size[SEC_TEXT];
    in->sec_base[SEC_DATA] = kind_size[SEC_TEXT] + kind_size[SEC_RODATA];
    in->sec_base[SEC_BSS] = kind_size[SEC_TEXT] + kind_size[SEC_RODATA] + kind_size[SEC_DATA];
    in->payload_size = kind_size[SEC_TEXT] + kind_size[SEC_RODATA] + kind_size[SEC_DATA];
    in->bss_size = kind_size[SEC_BSS];
    in->payload = (unsigned char *)malloc(in->payload_size ? in->payload_size : 1);
    if (!in->payload) {
        print("ld86: out of memory\n");
        return 0;
    }
    if (in->payload_size) memset(in->payload, 0, in->payload_size);

    for (unsigned int i = 0; i < shnum; i++) {
        const elf32_shdr_t *sh = &shdrs[i];
        int kind = sh_kind[i];
        if (kind < 0 || kind > (int)SEC_DATA) continue;
        if (sh->sh_type == ELF_SHT_NOBITS) continue;
        if (sh->sh_type != ELF_SHT_PROGBITS) continue;
        if (sh->sh_offset + sh->sh_size > in->size || sh->sh_offset + sh->sh_size < sh->sh_offset) {
            print("ld86: truncated ELF section data: ");
            print(in->path);
            print("\n");
            return 0;
        }
        unsigned int dst = in->sec_base[kind] + sh_off_in_kind[i];
        if (dst + sh->sh_size > in->payload_size || dst + sh->sh_size < dst) {
            print("ld86: ELF section overflow: ");
            print(in->path);
            print("\n");
            return 0;
        }
        memcpy(in->payload + dst, in->buf + sh->sh_offset, sh->sh_size);
    }

    int symtab_index = -1;
    for (unsigned int i = 0; i < shnum; i++) {
        if (shdrs[i].sh_type == ELF_SHT_SYMTAB) {
            symtab_index = (int)i;
            break;
        }
    }

    const elf32_sym_t *symtab = 0;
    const char *strtab = 0;
    unsigned int strtab_size = 0;
    if (symtab_index >= 0) {
        const elf32_shdr_t *symsh = &shdrs[symtab_index];
        if (symsh->sh_entsize != sizeof(elf32_sym_t) || symsh->sh_size % sizeof(elf32_sym_t) != 0) {
            print("ld86: bad ELF symtab: ");
            print(in->path);
            print("\n");
            return 0;
        }
        if (symsh->sh_offset + symsh->sh_size > in->size || symsh->sh_offset + symsh->sh_size < symsh->sh_offset) {
            print("ld86: truncated ELF symtab: ");
            print(in->path);
            print("\n");
            return 0;
        }
        if (symsh->sh_link >= shnum || shdrs[symsh->sh_link].sh_type != ELF_SHT_STRTAB) {
            print("ld86: bad ELF strtab link: ");
            print(in->path);
            print("\n");
            return 0;
        }
        const elf32_shdr_t *strsh = &shdrs[symsh->sh_link];
        if (strsh->sh_offset + strsh->sh_size > in->size || strsh->sh_offset + strsh->sh_size < strsh->sh_offset) {
            print("ld86: truncated ELF strtab: ");
            print(in->path);
            print("\n");
            return 0;
        }
        symtab = (const elf32_sym_t *)(in->buf + symsh->sh_offset);
        in->sym_count = symsh->sh_size / (unsigned int)sizeof(elf32_sym_t);
        strtab = (const char *)(in->buf + strsh->sh_offset);
        strtab_size = strsh->sh_size;
        in->syms = (mobj_sym_t *)malloc(in->sym_count * (unsigned int)sizeof(mobj_sym_t));
        if (!in->syms) {
            print("ld86: out of memory\n");
            return 0;
        }
        memset(in->syms, 0, in->sym_count * (unsigned int)sizeof(mobj_sym_t));
        for (unsigned int i = 0; i < in->sym_count; i++) {
            const elf32_sym_t *es = &symtab[i];
            mobj_sym_t *ms = &in->syms[i];
            unsigned int name_off = es->st_name;
            if (name_off < strtab_size) {
                int k = 0;
                while (k < 63 && name_off + (unsigned int)k < strtab_size && strtab[name_off + (unsigned int)k]) {
                    ms->name[k] = strtab[name_off + (unsigned int)k];
                    k++;
                }
                ms->name[k] = 0;
            }
            unsigned int bind = (unsigned int)(es->st_info >> 4);
            if (bind == ELF_STB_GLOBAL || bind == ELF_STB_WEAK) ms->flags |= MOBJ_SYM_GLOBAL;
            ms->section = SEC_UNDEF;
            ms->value_off = 0;
            if (es->st_shndx == ELF_SHN_UNDEF) continue;
            if (es->st_shndx == ELF_SHN_ABS) continue;
            if (es->st_shndx >= shnum) continue;
            int kind = sh_kind[es->st_shndx];
            if (kind < 0) continue;
            ms->section = (unsigned int)kind;
            ms->value_off = sh_off_in_kind[es->st_shndx] + es->st_value;
        }
    }

    unsigned int rel_count = 0;
    for (unsigned int i = 0; i < shnum; i++) {
        const elf32_shdr_t *sh = &shdrs[i];
        if (sh->sh_type != ELF_SHT_REL) continue;
        if (sh->sh_info >= shnum) continue;
        int kind = sh_kind[sh->sh_info];
        if (kind < 0 || kind > (int)SEC_DATA) continue;
        if (sh->sh_entsize != sizeof(elf32_rel_t) || sh->sh_size % sizeof(elf32_rel_t) != 0) {
            print("ld86: bad ELF reloc table: ");
            print(in->path);
            print("\n");
            return 0;
        }
        rel_count += sh->sh_size / (unsigned int)sizeof(elf32_rel_t);
    }
    in->rel_count = rel_count;
    if (rel_count) {
        if (!in->syms || in->sym_count == 0) {
            print("ld86: relocations require symtab: ");
            print(in->path);
            print("\n");
            return 0;
        }
        in->rels = (mobj_reloc_t *)malloc(rel_count * (unsigned int)sizeof(mobj_reloc_t));
        if (!in->rels) {
            print("ld86: out of memory\n");
            return 0;
        }
        unsigned int rw = 0;
        for (unsigned int i = 0; i < shnum; i++) {
            const elf32_shdr_t *sh = &shdrs[i];
            if (sh->sh_type != ELF_SHT_REL) continue;
            if (sh->sh_info >= shnum) continue;
            int kind = sh_kind[sh->sh_info];
            if (kind < 0 || kind > (int)SEC_DATA) continue;
            if (sh->sh_offset + sh->sh_size > in->size || sh->sh_offset + sh->sh_size < sh->sh_offset) {
                print("ld86: truncated ELF reloc data: ");
                print(in->path);
                print("\n");
                return 0;
            }
            const elf32_rel_t *rels = (const elf32_rel_t *)(in->buf + sh->sh_offset);
            unsigned int rc = sh->sh_size / (unsigned int)sizeof(elf32_rel_t);
            for (unsigned int j = 0; j < rc; j++) {
                unsigned int rtype = rels[j].r_info & 0xFFu;
                unsigned int rsym = rels[j].r_info >> 8;
                if (rsym >= in->sym_count) {
                    print("ld86: bad ELF reloc symbol index: ");
                    print(in->path);
                    print("\n");
                    return 0;
                }
                if (!(rtype == ELF_R_386_32 || rtype == ELF_R_386_PC32)) {
                    print("ld86: unsupported ELF relocation type in ");
                    print(in->path);
                    print(" type=");
                    print_num((int)rtype);
                    print("\n");
                    return 0;
                }
                unsigned int off = sh_off_in_kind[sh->sh_info] + rels[j].r_offset;
                unsigned int sec_lim = kind_size[kind];
                if (off + 4 > sec_lim || off + 4 < off) {
                    print("ld86: ELF relocation out of section range: ");
                    print(in->path);
                    print("\n");
                    return 0;
                }
                unsigned int place = in->sec_base[kind] + off;
                mobj_reloc_t *mr = &in->rels[rw++];
                mr->section = (unsigned int)kind;
                mr->offset = off;
                mr->type = (rtype == ELF_R_386_32) ? MOBJ_RELOC_ABS32 : MOBJ_RELOC_REL32;
                mr->sym_index = rsym;
                {
                    int add = (int)rd32(in->payload + place);
                    // ELF REL uses S + A - P for PC32, while ld86's internal
                    // REL32 path applies against (P+4). Shift by +4 so both
                    // models converge.
                    if (rtype == ELF_R_386_PC32) add += 4;
                    mr->addend = add;
                }
            }
        }
        if (rw != rel_count) {
            print("ld86: internal relocation count mismatch\n");
            return 0;
        }
    }
    return 1;
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
    int rn = fd_read(fd, buf, st.size);
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
    in->payload = in->buf;
    in->payload_size = in->size;
    if (is_mobj_magic(in->buf, in->size)) {
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

    if (is_elf_rel_object(in->buf, in->size)) {
        if (!parse_elf_rel_input(in)) return 0;
        return 1;
    }
    in->payload = in->buf;
    in->payload_size = in->size;
    in->is_obj = 0;
    in->obj_version = 0;
    in->entry_off = 0;
    in->bss_size = 0;
    return 1;
}

static int resolve_symbol_addr(input_t *inputs, int input_count,
                               int owner_idx, unsigned int sym_idx,
                               unsigned int ref_section, unsigned int ref_offset,
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
    int found_input = -1;
    for (int i = 0; i < input_count; i++) {
        input_t *cand = &inputs[i];
        if (!cand->is_obj || cand->obj_version < 2) continue;
        for (unsigned int j = 0; j < cand->sym_count; j++) {
            mobj_sym_t *cs = &cand->syms[j];
            if (cs->section == SEC_UNDEF) continue;
            if (!(cs->flags & MOBJ_SYM_GLOBAL)) continue;
            if (!sym_name_eq_loose(cs->name, s->name)) continue;
            unsigned int addr = base + cand->image_off + cand->sec_base[cs->section] + cs->value_off;
            if (found) {
                // Allow multiple loose-name aliases within the same object
                // (e.g. "$print" and "print" in libc.o).
                if (found_input == i) {
                    continue;
                }
                print("ld86: duplicate global symbol: ");
                print(s->name);
                print(" provided by ");
                print(inputs[found_input].path);
                print(" and ");
                print(cand->path);
                print("\n");
                return 0;
            }
            found = 1;
            found_addr = addr;
            found_input = i;
        }
    }
    if (!found) {
        // Fallback: some imported ELF objects may not mark aliases as global.
        // For unresolved externs, allow a loose-name match against any defined
        // symbol (e.g. "$print" <-> "print").
        const char *want = sym_strip_dollar(s->name);
        for (int i = 0; i < input_count && !found; i++) {
            input_t *cand = &inputs[i];
            if (!cand->is_obj || cand->obj_version < 2) continue;
            for (unsigned int j = 0; j < cand->sym_count; j++) {
                mobj_sym_t *cs = &cand->syms[j];
                if (cs->section == SEC_UNDEF) continue;
                if (strcmp(sym_strip_dollar(cs->name), want) != 0) continue;
                found = 1;
                found_addr = base + cand->image_off + cand->sec_base[cs->section] + cs->value_off;
                found_input = i;
                break;
            }
        }
    }

    if (!found) {
        print("ld86: undefined symbol: ");
        print(s->name);
        print(" referenced by ");
        print(owner->path);
        print(" sec=");
        print_num((int)ref_section);
        print(" off=");
        print_hex(ref_offset);
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
    int input_count = 0;

    unsigned int total_file = 0;
    unsigned int total_mem = 0;
    int chosen_entry = 0;

    for (int i = 0; i < pos_count; i++) {
        unsigned int sz = 0;
        unsigned char *buf = read_whole_file(pos[i], &sz);
        if (!buf) {
            print("ld86: cannot read input: ");
            print(pos[i]);
            print("\n");
            exit(1);
        }
        if (is_ar_archive(buf, sz)) {
            unsigned int off = 8;
            while (off + (unsigned int)sizeof(ar_hdr_t) <= sz) {
                const ar_hdr_t *ah = (const ar_hdr_t *)(buf + off);
                unsigned int msz = 0;
                if (!parse_u32_dec_field(ah->size, 10, &msz)) {
                    print("ld86: bad archive member size in ");
                    print(pos[i]);
                    print("\n");
                    exit(1);
                }
                unsigned int data_off = off + (unsigned int)sizeof(ar_hdr_t);
                if (data_off + msz > sz || data_off + msz < data_off) {
                    print("ld86: truncated archive member in ");
                    print(pos[i]);
                    print("\n");
                    exit(1);
                }
                int is_special = 0;
                if (ah->name[0] == '/' || (ah->name[0] == '#' && ah->name[1] == '1')) is_special = 1;
                if (!is_special && msz > 0) {
                    if (input_count >= MAX_INPUTS) {
                        print("ld86: too many expanded inputs\n");
                        exit(1);
                    }
                    char member[24];
                    ar_member_name(ah, member, sizeof(member));
                    set_input_path(&inputs[input_count], pos[i], member);
                    inputs[input_count].buf = (unsigned char *)malloc(msz);
                    if (!inputs[input_count].buf) {
                        print("ld86: out of memory\n");
                        exit(1);
                    }
                    memcpy(inputs[input_count].buf, buf + data_off, msz);
                    inputs[input_count].size = msz;
                    if (!parse_input(&inputs[input_count])) exit(1);
                    inputs[input_count].image_off = total_file;
                    total_file += inputs[input_count].payload_size;
                    total_mem += inputs[input_count].payload_size + inputs[input_count].bss_size;
                    if (!entry_set && !chosen_entry && inputs[input_count].is_obj) {
                        entry = base + inputs[input_count].image_off + inputs[input_count].entry_off;
                        chosen_entry = 1;
                    }
                    input_count++;
                }
                off = data_off + msz;
                if (off & 1u) off++;
            }
        } else {
            if (input_count >= MAX_INPUTS) {
                print("ld86: too many input files\n");
                exit(1);
            }
            set_input_path(&inputs[input_count], pos[i], 0);
            inputs[input_count].buf = buf;
            inputs[input_count].size = sz;
            if (!parse_input(&inputs[input_count])) exit(1);

            inputs[input_count].image_off = total_file;
            total_file += inputs[input_count].payload_size;
            total_mem += inputs[input_count].payload_size + inputs[input_count].bss_size;

            if (!entry_set && !chosen_entry && inputs[input_count].is_obj) {
                entry = base + inputs[input_count].image_off + inputs[input_count].entry_off;
                chosen_entry = 1;
            }
            input_count++;
        }
    }

    if (input_count <= 0) {
        print("ld86: no linkable inputs\n");
        exit(1);
    }

    unsigned char *image = (unsigned char *)malloc(total_file > 0 ? total_file : 1);
    if (!image) {
        print("ld86: out of memory\n");
        exit(1);
    }
    if (total_file) memset(image, 0, total_file);

    for (int i = 0; i < input_count; i++) {
        if (inputs[i].payload_size > 0) {
            memcpy(image + inputs[i].image_off, inputs[i].payload, inputs[i].payload_size);
        }
    }

    for (int i = 0; i < input_count; i++) {
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
            if (!resolve_symbol_addr(inputs, input_count, i, r->sym_index,
                                     r->section, r->offset, base, &sym_addr)) {
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
    if (fd_write(ofd, obuf, out_sz) != (int)out_sz) {
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
