#include "syscalls.h"
#include "libc.h"

#define MAX_SRC      (512 * 1024)
#define MAX_LINE     1024
#define MAX_TOK      256
#define MAX_LABELS   2048
#define MAX_NAME     64
#define MAX_RELOCS   8192

#define MOBJ_SYM_GLOBAL 0x1u
#define MOBJ_SYM_EXTERN 0x2u

#define MOBJ_RELOC_ABS32 1u
#define MOBJ_RELOC_REL32 2u

#define SEC_UNDEF 0xFFFFFFFFu

typedef struct __attribute__((packed)) {
    unsigned char magic[4];   // "MOBJ"
    unsigned int version;     // 2
    unsigned int org;
    unsigned int entry_off;   // offset from start of flattened image
    unsigned int text_size;
    unsigned int rodata_size;
    unsigned int data_size;
    unsigned int bss_size;
    unsigned int sym_count;
    unsigned int reloc_count;
} mobj_header_t;

typedef struct __attribute__((packed)) {
    char name[MAX_NAME];
    unsigned int value_off;
    unsigned int section; // SEC_*, or SEC_UNDEF
    unsigned int flags;   // MOBJ_SYM_*
} mobj_sym_t;

typedef struct __attribute__((packed)) {
    unsigned int section; // SEC_*
    unsigned int offset;  // byte offset in section
    unsigned int type;    // MOBJ_RELOC_*
    unsigned int sym_index;
    int addend;
} mobj_reloc_t;

typedef struct {
    char name[MAX_NAME];
    unsigned int offset;
    int section;
    int defined;
    int is_global;
    int is_extern;
} label_t;

typedef enum {
    OP_NONE = 0,
    OP_REG,
    OP_IMM,
    OP_LABEL,
    OP_MEM
} op_kind_t;

typedef struct {
    op_kind_t kind;
    int reg;           // 0..7
    int reg_bits;      // 8,16,32
    int imm;
    char label[MAX_NAME];
    struct {
        int base_reg;        // -1 for none
        int disp;
        char disp_label[MAX_NAME];
        int size_hint;       // 0,8,16,32
    } mem;
} operand_t;

enum {
    SEC_TEXT = 0,
    SEC_RODATA = 1,
    SEC_DATA = 2,
    SEC_BSS = 3,
    SEC_COUNT = 4
};

typedef struct {
    const char *src;
    int src_len;
    int pass;
    unsigned int org;
    int cur_sec;
    unsigned int sec_pc[SEC_COUNT];
    unsigned int sec_size[SEC_COUNT];
    unsigned int sec_base[SEC_COUNT];
    unsigned char *sec_out[SEC_COUNT];
    int sec_cap[SEC_COUNT];
    int sec_len[SEC_COUNT];
    unsigned char *out;
    int out_len;
    label_t labels[MAX_LABELS];
    int label_count;
    int had_error;
    int meaningful_lines;
    int line_no;
    char cur_line[MAX_LINE];
    int fmt_obj;
    mobj_reloc_t relocs[MAX_RELOCS];
    int reloc_count;
} asm_ctx_t;

static int get_label_index(asm_ctx_t *ctx, const char *name, int create);

static unsigned int cur_pc(asm_ctx_t *ctx) {
    return ctx->org + ctx->sec_base[ctx->cur_sec] + ctx->sec_pc[ctx->cur_sec];
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int streqi(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (to_lower(a[i]) != to_lower(b[i])) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int find_label_addr(asm_ctx_t *ctx, const char *name, unsigned int *addr_out) {
    int idx = get_label_index(ctx, name, 0);
    if (idx < 0 || !ctx->labels[idx].defined) return 0;
    *addr_out = ctx->org + ctx->sec_base[ctx->labels[idx].section] + ctx->labels[idx].offset;
    return 1;
}

static void copy_lim(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static char *find_char(char *s, char ch) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == ch) return &s[i];
    }
    return 0;
}

static void print_err(const char *msg) {
    print("as86: ");
    print(msg);
    print("\n");
}

static void print_err2(const char *a, const char *b) {
    print("as86: ");
    print(a);
    print(b);
    print("\n");
}

static void rtrim(char *s) {
    int n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[n - 1] = 0;
            n--;
        } else {
            break;
        }
    }
}

static char *ltrim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

static int split_operands(char *s, char out[][MAX_TOK], int max_out) {
    int n = 0;
    int level = 0;
    int in_str = 0;
    int start = 0;
    int len = strlen(s);
    for (int i = 0; i <= len; i++) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i - 1] != '\\')) in_str = !in_str;
        if (!in_str) {
            if (c == '[') level++;
            else if (c == ']' && level > 0) level--;
        }
        if ((c == ',' && level == 0 && !in_str) || c == 0) {
            if (n < max_out) {
                int tlen = i - start;
                if (tlen >= MAX_TOK) tlen = MAX_TOK - 1;
                memcpy(out[n], s + start, (unsigned int)tlen);
                out[n][tlen] = 0;
                char *t = ltrim(out[n]);
                if (t != out[n]) {
                    int k = 0;
                    while (t[k]) { out[n][k] = t[k]; k++; }
                    out[n][k] = 0;
                }
                rtrim(out[n]);
                n++;
            }
            start = i + 1;
        }
    }
    return n;
}

static int parse_int(const char *s, int *out) {
    int sign = 1;
    int i = 0;
    int base = 10;
    int v = 0;
    if (s[0] == '-') {
        sign = -1;
        i++;
    } else if (s[0] == '+') {
        i++;
    }
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

static int reg_code_bits(const char *s, int *code, int *bits) {
    static const char *r8[] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
    static const char *r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
    static const char *r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (int i = 0; i < 8; i++) {
        if (streqi(s, r8[i])) { *code = i; *bits = 8; return 1; }
    }
    for (int i = 0; i < 8; i++) {
        if (streqi(s, r16[i])) { *code = i; *bits = 16; return 1; }
    }
    for (int i = 0; i < 8; i++) {
        if (streqi(s, r32[i])) { *code = i; *bits = 32; return 1; }
    }
    return 0;
}

static int get_label_index(asm_ctx_t *ctx, const char *name, int create) {
    for (int i = 0; i < ctx->label_count; i++) {
        if (streq(ctx->labels[i].name, name)) return i;
    }
    if (!create) return -1;
    if (ctx->label_count >= MAX_LABELS) return -1;
    int idx = ctx->label_count++;
    memset(&ctx->labels[idx], 0, sizeof(label_t));
    copy_lim(ctx->labels[idx].name, name, MAX_NAME);
    return idx;
}

static void mark_label_global(asm_ctx_t *ctx, const char *name) {
    int idx = get_label_index(ctx, name, 1);
    if (idx >= 0) ctx->labels[idx].is_global = 1;
}

static void mark_label_extern(asm_ctx_t *ctx, const char *name) {
    int idx = get_label_index(ctx, name, 1);
    if (idx >= 0) ctx->labels[idx].is_extern = 1;
}

static int add_reloc(asm_ctx_t *ctx, unsigned int section, unsigned int offset,
                     unsigned int type, const char *sym_name, int addend) {
    if (!ctx->fmt_obj || ctx->pass != 2) return 1;
    if (ctx->reloc_count >= MAX_RELOCS) {
        print_err("too many relocations");
        ctx->had_error = 1;
        return 0;
    }
    int sym_idx = get_label_index(ctx, sym_name, 1);
    if (sym_idx < 0) {
        print_err("symbol table overflow");
        ctx->had_error = 1;
        return 0;
    }
    mobj_reloc_t *r = &ctx->relocs[ctx->reloc_count++];
    r->section = section;
    r->offset = offset;
    r->type = type;
    r->sym_index = (unsigned int)sym_idx;
    r->addend = addend;
    return 1;
}

static int define_label(asm_ctx_t *ctx, const char *name, unsigned int offset, int section) {
    int idx = get_label_index(ctx, name, 1);
    if (idx < 0) return 0;
    if (ctx->labels[idx].defined && ctx->pass == 1) {
        print_err2("duplicate label: ", name);
        ctx->had_error = 1;
        return 0;
    }
    ctx->labels[idx].offset = offset;
    ctx->labels[idx].section = section;
    ctx->labels[idx].defined = 1;
    return 1;
}

static int resolve_label(asm_ctx_t *ctx, const char *name, int *out) {
    int idx = get_label_index(ctx, name, 0);
    if (idx < 0 || !ctx->labels[idx].defined) {
        if (ctx->fmt_obj && ctx->pass == 2 && idx >= 0 && ctx->labels[idx].is_extern) {
            *out = 0;
            return 1;
        }
        if (ctx->pass == 2) {
            print_err2("undefined label: ", name);
            ctx->had_error = 1;
        }
        *out = 0;
        return 0;
    }
    *out = (int)(ctx->org + ctx->sec_base[ctx->labels[idx].section] + ctx->labels[idx].offset);
    return 1;
}

static int emit8(asm_ctx_t *ctx, unsigned int v) {
    if (ctx->pass == 2) {
        int s = ctx->cur_sec;
        if (!ctx->sec_out[s] || ctx->sec_len[s] >= ctx->sec_cap[s]) {
            int new_cap = ctx->sec_cap[s] > 0 ? (ctx->sec_cap[s] * 2) : 1024;
            if (new_cap < ctx->sec_len[s] + 1) new_cap = ctx->sec_len[s] + 1;
            unsigned char *nb = (unsigned char *)realloc(ctx->sec_out[s], (unsigned int)new_cap);
            if (!nb) {
                print_err("output too large");
                ctx->had_error = 1;
                return 0;
            }
            // Zero-fill the newly added region for deterministic output.
            if (new_cap > ctx->sec_cap[s]) {
                memset(nb + ctx->sec_cap[s], 0, (unsigned int)(new_cap - ctx->sec_cap[s]));
            }
            ctx->sec_out[s] = nb;
            ctx->sec_cap[s] = new_cap;
        }
        ctx->sec_out[s][ctx->sec_len[s]++] = (unsigned char)(v & 0xFFu);
    }
    ctx->sec_pc[ctx->cur_sec]++;
    return 1;
}

static int emit16(asm_ctx_t *ctx, unsigned int v) {
    return emit8(ctx, v & 0xFFu) && emit8(ctx, (v >> 8) & 0xFFu);
}

static int emit32(asm_ctx_t *ctx, unsigned int v) {
    return emit8(ctx, v & 0xFFu) &&
           emit8(ctx, (v >> 8) & 0xFFu) &&
           emit8(ctx, (v >> 16) & 0xFFu) &&
           emit8(ctx, (v >> 24) & 0xFFu);
}

static int emit_modrm(asm_ctx_t *ctx, int mod, int reg, int rm) {
    return emit8(ctx, (unsigned int)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7)));
}

static int parse_mem_expr(char *expr, operand_t *op) {
    op->kind = OP_MEM;
    op->mem.base_reg = -1;
    op->mem.disp = 0;
    op->mem.disp_label[0] = 0;

    char buf[MAX_TOK];
    int p = 0;
    for (int i = 0; expr[i] && p < MAX_TOK - 1; i++) {
        if (expr[i] != ' ' && expr[i] != '\t') buf[p++] = expr[i];
    }
    buf[p] = 0;
    if (!buf[0]) return 0;

    int i = 0;
    int sign = +1;
    while (buf[i]) {
        int j = i;
        while (buf[j] && buf[j] != '+' && buf[j] != '-') j++;
        char term[MAX_TOK];
        int tlen = j - i;
        if (tlen <= 0 || tlen >= MAX_TOK) return 0;
        memcpy(term, buf + i, (unsigned int)tlen);
        term[tlen] = 0;

        int rc, rb;
        int val;
        if (reg_code_bits(term, &rc, &rb) && rb == 32) {
            if (op->mem.base_reg != -1) return 0;
            op->mem.base_reg = rc;
        } else if (parse_int(term, &val)) {
            op->mem.disp += sign * val;
        } else {
            if (sign < 0) return 0; // label subtraction not supported in phase-1
            if (op->mem.disp_label[0]) return 0;
            copy_lim(op->mem.disp_label, term, MAX_NAME);
        }

        if (!buf[j]) break;
        sign = (buf[j] == '-') ? -1 : +1;
        i = j + 1;
    }
    return 1;
}

static int parse_operand(char *s, operand_t *op) {
    memset(op, 0, sizeof(*op));
    op->kind = OP_NONE;
    op->mem.base_reg = -1;
    op->mem.size_hint = 0;

    s = ltrim(s);
    rtrim(s);
    if (!s[0]) return 1;

    if (!strncmp(s, "byte ", 5)) {
        op->mem.size_hint = 8;
        s = ltrim(s + 5);
    } else if (!strncmp(s, "word ", 5)) {
        op->mem.size_hint = 16;
        s = ltrim(s + 5);
    } else if (!strncmp(s, "dword ", 6)) {
        op->mem.size_hint = 32;
        s = ltrim(s + 6);
    }

    int n = strlen(s);
    if (n >= 2 && s[0] == '[' && s[n - 1] == ']') {
        char inner[MAX_TOK];
        if (n - 2 >= MAX_TOK) return 0;
        memcpy(inner, s + 1, (unsigned int)(n - 2));
        inner[n - 2] = 0;
        if (!parse_mem_expr(inner, op)) return 0;
        return 1;
    }

    int reg, bits;
    if (reg_code_bits(s, &reg, &bits)) {
        op->kind = OP_REG;
        op->reg = reg;
        op->reg_bits = bits;
        return 1;
    }

    int val;
    if (parse_int(s, &val)) {
        op->kind = OP_IMM;
        op->imm = val;
        return 1;
    }

    op->kind = OP_LABEL;
    copy_lim(op->label, s, MAX_NAME);
    return 1;
}

static int resolve_imm(asm_ctx_t *ctx, operand_t *op, int *out) {
    if (op->kind == OP_IMM) {
        *out = op->imm;
        return 1;
    }
    if (op->kind == OP_LABEL) {
        if (ctx->pass == 1) {
            // Use a non-small placeholder to force wide immediate encodings.
            // If we used 0, pass 1 might choose imm8 forms (e.g. push 0x00),
            // shrinking code and skewing forward label addresses for pass 2.
            *out = 0x1000;
            return 1;
        }
        return resolve_label(ctx, op->label, out);
    }
    return 0;
}

static int emit_rm_operand(asm_ctx_t *ctx, int reg_field, operand_t *rmop) {
    if (rmop->kind == OP_REG) {
        return emit_modrm(ctx, 3, reg_field, rmop->reg);
    }
    if (rmop->kind != OP_MEM) return 0;

    if (rmop->mem.base_reg == -1) {
        if (!emit_modrm(ctx, 0, reg_field, 5)) return 0;
        int addr = rmop->mem.disp;
        if (rmop->mem.disp_label[0]) {
            int l = 0;
            if (!resolve_label(ctx, rmop->mem.disp_label, &l)) return 0;
            addr += l;
            unsigned int off = ctx->sec_pc[ctx->cur_sec];
            if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_ABS32, rmop->mem.disp_label, rmop->mem.disp)) {
                return 0;
            }
            if (ctx->fmt_obj) {
                // Relocation addend already carries disp, keep encoded placeholder zero.
                addr = 0;
            }
        }
        return emit32(ctx, (unsigned int)addr);
    }

    if (rmop->mem.disp_label[0]) {
        print_err("base+label memory form unsupported");
        ctx->had_error = 1;
        return 0;
    }

    int base = rmop->mem.base_reg;
    int disp = rmop->mem.disp;
    int mod = 0;
    int need_sib = (base == 4);

    if (disp == 0 && base != 5) mod = 0;
    else if (disp >= -128 && disp <= 127) mod = 1;
    else mod = 2;

    int rm = need_sib ? 4 : base;
    if (!emit_modrm(ctx, mod, reg_field, rm)) return 0;

    if (need_sib) {
        // ss=00, index=100 (none), base=esp
        if (!emit8(ctx, 0x24)) return 0;
    }

    if (mod == 1) return emit8(ctx, (unsigned int)(disp & 0xFF));
    if (mod == 2 || (mod == 0 && base == 5)) return emit32(ctx, (unsigned int)disp);
    return 1;
}

static int maybe_prefix_16(asm_ctx_t *ctx, int bits) {
    if (bits == 16) return emit8(ctx, 0x66);
    return 1;
}

static int is_jcc(const char *m, int *cc) {
    struct { const char *n; int c; } table[] = {
        {"jo",0}, {"jno",1}, {"jb",2}, {"jc",2}, {"jnae",2},
        {"jnb",3}, {"jnc",3}, {"jae",3}, {"je",4}, {"jz",4},
        {"jne",5}, {"jnz",5}, {"jbe",6}, {"jna",6}, {"ja",7},
        {"jnbe",7}, {"js",8}, {"jns",9}, {"jp",10}, {"jpe",10},
        {"jnp",11}, {"jpo",11}, {"jl",12}, {"jnge",12},
        {"jge",13}, {"jnl",13}, {"jle",14}, {"jng",14},
        {"jg",15}, {"jnle",15}
    };
    for (unsigned int i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (streqi(m, table[i].n)) { *cc = table[i].c; return 1; }
    }
    return 0;
}

static int is_setcc(const char *m, int *cc) {
    struct { const char *n; int c; } table[] = {
        {"seto",0}, {"setno",1}, {"setb",2}, {"setc",2}, {"setnae",2},
        {"setnb",3}, {"setnc",3}, {"setae",3}, {"sete",4}, {"setz",4},
        {"setne",5}, {"setnz",5}, {"setbe",6}, {"setna",6}, {"seta",7},
        {"setnbe",7}, {"sets",8}, {"setns",9}, {"setp",10}, {"setpe",10},
        {"setnp",11}, {"setpo",11}, {"setl",12}, {"setnge",12},
        {"setge",13}, {"setnl",13}, {"setle",14}, {"setng",14},
        {"setg",15}, {"setnle",15}
    };
    for (unsigned int i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (streqi(m, table[i].n)) { *cc = table[i].c; return 1; }
    }
    return 0;
}

static int encode_binop_rm_reg(asm_ctx_t *ctx, unsigned char opc, operand_t *a, operand_t *b) {
    // a = r/m, b = reg
    if (!emit8(ctx, opc)) return 0;
    return emit_rm_operand(ctx, b->reg, a);
}

static int encode_binop_reg_rm(asm_ctx_t *ctx, unsigned char opc, operand_t *a, operand_t *b) {
    // a = reg, b = r/m
    if (!emit8(ctx, opc)) return 0;
    return emit_rm_operand(ctx, a->reg, b);
}

static int encode_grp1_imm(asm_ctx_t *ctx, int ext, operand_t *dst, int imm, int bits) {
    if (bits == 8) {
        if (!emit8(ctx, 0x80)) return 0;
        if (!emit_rm_operand(ctx, ext, dst)) return 0;
        return emit8(ctx, (unsigned int)(imm & 0xFF));
    }
    if (imm >= -128 && imm <= 127) {
        if (!maybe_prefix_16(ctx, bits)) return 0;
        if (!emit8(ctx, 0x83)) return 0;
        if (!emit_rm_operand(ctx, ext, dst)) return 0;
        return emit8(ctx, (unsigned int)(imm & 0xFF));
    }
    if (!maybe_prefix_16(ctx, bits)) return 0;
    if (!emit8(ctx, 0x81)) return 0;
    if (!emit_rm_operand(ctx, ext, dst)) return 0;
    if (bits == 16) return emit16(ctx, (unsigned int)imm);
    return emit32(ctx, (unsigned int)imm);
}

static int infer_bits_from_operand(operand_t *op, int fallback) {
    if (op->kind == OP_REG) return op->reg_bits;
    if (op->kind == OP_MEM && op->mem.size_hint) return op->mem.size_hint;
    return fallback;
}

static int encode_instruction(asm_ctx_t *ctx, char *mn, operand_t *ops, int opn) {
    int cc = 0;
    int imm = 0;

    if (streqi(mn, "nop") && opn == 0) return emit8(ctx, 0x90);
    if (streqi(mn, "ret") && opn == 0) return emit8(ctx, 0xC3);
    if (streqi(mn, "leave") && opn == 0) return emit8(ctx, 0xC9);
    if (streqi(mn, "cdq") && opn == 0) return emit8(ctx, 0x99);
    if (streqi(mn, "cbw") && opn == 0) return emit8(ctx, 0x98);
    if (streqi(mn, "pushad") && opn == 0) return emit8(ctx, 0x60);
    if (streqi(mn, "popad") && opn == 0) return emit8(ctx, 0x61);
    if (streqi(mn, "iret") && opn == 0) return emit8(ctx, 0xCF);

    if (streqi(mn, "int") && opn == 1) {
        if (!resolve_imm(ctx, &ops[0], &imm)) return 0;
        return emit8(ctx, 0xCD) && emit8(ctx, (unsigned int)(imm & 0xFF));
    }

    if (streqi(mn, "push") && opn == 1) {
        if (ops[0].kind == OP_REG) return emit8(ctx, (unsigned int)(0x50 + ops[0].reg));
        if (ops[0].kind == OP_IMM || ops[0].kind == OP_LABEL) {
            if (ctx->fmt_obj && ops[0].kind == OP_LABEL) {
                if (!emit8(ctx, 0x68)) return 0;
                unsigned int off = ctx->sec_pc[ctx->cur_sec];
                if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_ABS32, ops[0].label, 0)) return 0;
                return emit32(ctx, 0);
            }
            if (!resolve_imm(ctx, &ops[0], &imm)) return 0;
            if (imm >= -128 && imm <= 127) return emit8(ctx, 0x6A) && emit8(ctx, (unsigned int)(imm & 0xFF));
            return emit8(ctx, 0x68) && emit32(ctx, (unsigned int)imm);
        }
        if (ops[0].kind == OP_MEM) return emit8(ctx, 0xFF) && emit_rm_operand(ctx, 6, &ops[0]);
    }

    if (streqi(mn, "pop") && opn == 1) {
        if (ops[0].kind == OP_REG) return emit8(ctx, (unsigned int)(0x58 + ops[0].reg));
        if (ops[0].kind == OP_MEM) return emit8(ctx, 0x8F) && emit_rm_operand(ctx, 0, &ops[0]);
    }

    if (streqi(mn, "call") && opn == 1) {
        if (ops[0].kind == OP_LABEL || ops[0].kind == OP_IMM) {
            if (ctx->fmt_obj && ops[0].kind == OP_LABEL) {
                if (!emit8(ctx, 0xE8)) return 0;
                unsigned int off = ctx->sec_pc[ctx->cur_sec];
                if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_REL32, ops[0].label, 0)) return 0;
                return emit32(ctx, 0);
            }
            if (!resolve_imm(ctx, &ops[0], &imm)) return 0;
            int rel = imm - ((int)cur_pc(ctx) + 5);
            return emit8(ctx, 0xE8) && emit32(ctx, (unsigned int)rel);
        }
        return emit8(ctx, 0xFF) && emit_rm_operand(ctx, 2, &ops[0]);
    }

    if (streqi(mn, "jmp") && opn == 1) {
        if (ops[0].kind == OP_LABEL || ops[0].kind == OP_IMM) {
            if (ctx->fmt_obj && ops[0].kind == OP_LABEL) {
                if (!emit8(ctx, 0xE9)) return 0;
                unsigned int off = ctx->sec_pc[ctx->cur_sec];
                if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_REL32, ops[0].label, 0)) return 0;
                return emit32(ctx, 0);
            }
            if (!resolve_imm(ctx, &ops[0], &imm)) return 0;
            int rel = imm - ((int)cur_pc(ctx) + 5);
            return emit8(ctx, 0xE9) && emit32(ctx, (unsigned int)rel);
        }
        return emit8(ctx, 0xFF) && emit_rm_operand(ctx, 4, &ops[0]);
    }

    if (is_jcc(mn, &cc) && opn == 1) {
        if (ctx->fmt_obj && ops[0].kind == OP_LABEL) {
            if (!emit8(ctx, 0x0F) || !emit8(ctx, (unsigned int)(0x80 + cc))) return 0;
            unsigned int off = ctx->sec_pc[ctx->cur_sec];
            if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_REL32, ops[0].label, 0)) return 0;
            return emit32(ctx, 0);
        }
        if (!resolve_imm(ctx, &ops[0], &imm)) return 0;
        int rel = imm - ((int)cur_pc(ctx) + 6);
        return emit8(ctx, 0x0F) && emit8(ctx, (unsigned int)(0x80 + cc)) && emit32(ctx, (unsigned int)rel);
    }

    if (is_setcc(mn, &cc) && opn == 1) {
        int bits = infer_bits_from_operand(&ops[0], 8);
        if (bits != 8) {
            print_err("setcc destination must be 8-bit");
            ctx->had_error = 1;
            return 0;
        }
        return emit8(ctx, 0x0F) && emit8(ctx, (unsigned int)(0x90 + cc)) && emit_rm_operand(ctx, 0, &ops[0]);
    }

    if (streqi(mn, "inc") && opn == 1) return emit8(ctx, 0xFF) && emit_rm_operand(ctx, 0, &ops[0]);
    if (streqi(mn, "dec") && opn == 1) return emit8(ctx, 0xFF) && emit_rm_operand(ctx, 1, &ops[0]);
    if (streqi(mn, "not") && opn == 1) return emit8(ctx, 0xF7) && emit_rm_operand(ctx, 2, &ops[0]);
    if (streqi(mn, "neg") && opn == 1) return emit8(ctx, 0xF7) && emit_rm_operand(ctx, 3, &ops[0]);
    if (streqi(mn, "mul") && opn == 1) return emit8(ctx, 0xF7) && emit_rm_operand(ctx, 4, &ops[0]);
    if (streqi(mn, "imul") && opn == 1) return emit8(ctx, 0xF7) && emit_rm_operand(ctx, 5, &ops[0]);
    if (streqi(mn, "idiv") && opn == 1) return emit8(ctx, 0xF7) && emit_rm_operand(ctx, 7, &ops[0]);

    if (streqi(mn, "mov") && opn == 2) {
        int bits = infer_bits_from_operand(&ops[0], infer_bits_from_operand(&ops[1], 32));
        if (ops[0].kind == OP_REG && (ops[1].kind == OP_IMM || ops[1].kind == OP_LABEL)) {
            if (ctx->fmt_obj && ops[1].kind == OP_LABEL && bits == 32) {
                if (!maybe_prefix_16(ctx, bits)) return 0;
                if (!emit8(ctx, (unsigned int)(0xB8 + ops[0].reg))) return 0;
                unsigned int off = ctx->sec_pc[ctx->cur_sec];
                if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_ABS32, ops[1].label, 0)) return 0;
                return emit32(ctx, 0);
            }
            if (!resolve_imm(ctx, &ops[1], &imm)) return 0;
            if (!maybe_prefix_16(ctx, bits)) return 0;
            return emit8(ctx, (unsigned int)(0xB8 + ops[0].reg)) &&
                   (bits == 16 ? emit16(ctx, (unsigned int)imm) : emit32(ctx, (unsigned int)imm));
        }
        if (ops[0].kind == OP_REG && (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
            if (!maybe_prefix_16(ctx, bits)) return 0;
            return emit8(ctx, (bits == 8) ? 0x8A : 0x8B) && emit_rm_operand(ctx, ops[0].reg, &ops[1]);
        }
        if ((ops[0].kind == OP_REG || ops[0].kind == OP_MEM) && ops[1].kind == OP_REG) {
            if (!maybe_prefix_16(ctx, bits)) return 0;
            return emit8(ctx, (bits == 8) ? 0x88 : 0x89) && emit_rm_operand(ctx, ops[1].reg, &ops[0]);
        }
        if (ops[0].kind == OP_MEM && (ops[1].kind == OP_IMM || ops[1].kind == OP_LABEL)) {
            if (ctx->fmt_obj && ops[1].kind == OP_LABEL) {
                bits = infer_bits_from_operand(&ops[0], 32);
                if (bits != 32) {
                    print_err("obj reloc supports only 32-bit mem immediates");
                    ctx->had_error = 1;
                    return 0;
                }
                if (!maybe_prefix_16(ctx, bits)) return 0;
                if (!emit8(ctx, 0xC7) || !emit_rm_operand(ctx, 0, &ops[0])) return 0;
                unsigned int off = ctx->sec_pc[ctx->cur_sec];
                if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_ABS32, ops[1].label, 0)) return 0;
                return emit32(ctx, 0);
            }
            if (!resolve_imm(ctx, &ops[1], &imm)) return 0;
            bits = infer_bits_from_operand(&ops[0], 32);
            if (!maybe_prefix_16(ctx, bits)) return 0;
            if (bits == 8) return emit8(ctx, 0xC6) && emit_rm_operand(ctx, 0, &ops[0]) && emit8(ctx, (unsigned int)(imm & 0xFF));
            return emit8(ctx, 0xC7) && emit_rm_operand(ctx, 0, &ops[0]) &&
                   (bits == 16 ? emit16(ctx, (unsigned int)imm) : emit32(ctx, (unsigned int)imm));
        }
    }

    if (streqi(mn, "lea") && opn == 2 && ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
        return emit8(ctx, 0x8D) && emit_rm_operand(ctx, ops[0].reg, &ops[1]);
    }

    if ((streqi(mn, "movsx") || streqi(mn, "movzx")) && opn == 2 &&
        ops[0].kind == OP_REG && (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
        int src_bits = infer_bits_from_operand(&ops[1], ops[1].kind == OP_REG ? ops[1].reg_bits : 8);
        int op2 = streqi(mn, "movsx") ? 0xBE : 0xB6;
        if (src_bits == 16) op2++;
        return emit8(ctx, 0x0F) && emit8(ctx, (unsigned int)op2) && emit_rm_operand(ctx, ops[0].reg, &ops[1]);
    }

    if (streqi(mn, "imul") && opn == 2 && ops[0].kind == OP_REG &&
        (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
        return emit8(ctx, 0x0F) && emit8(ctx, 0xAF) && emit_rm_operand(ctx, ops[0].reg, &ops[1]);
    }

    if (streqi(mn, "imul") && opn == 3 && ops[0].kind == OP_REG &&
        (ops[1].kind == OP_REG || ops[1].kind == OP_MEM) &&
        (ops[2].kind == OP_IMM || ops[2].kind == OP_LABEL)) {
        if (!resolve_imm(ctx, &ops[2], &imm)) return 0;
        if (imm >= -128 && imm <= 127) {
            return emit8(ctx, 0x6B) && emit_rm_operand(ctx, ops[0].reg, &ops[1]) && emit8(ctx, (unsigned int)(imm & 0xFF));
        }
        return emit8(ctx, 0x69) && emit_rm_operand(ctx, ops[0].reg, &ops[1]) && emit32(ctx, (unsigned int)imm);
    }

    struct {
        const char *n;
        unsigned char rm_reg;
        unsigned char reg_rm;
        int grp_ext;
    } alu_ops[] = {
        {"add", 0x01, 0x03, 0},
        {"or",  0x09, 0x0B, 1},
        {"adc", 0x11, 0x13, 2},
        {"sbb", 0x19, 0x1B, 3},
        {"and", 0x21, 0x23, 4},
        {"sub", 0x29, 0x2B, 5},
        {"xor", 0x31, 0x33, 6},
        {"cmp", 0x39, 0x3B, 7}
    };
    for (unsigned int i = 0; i < sizeof(alu_ops)/sizeof(alu_ops[0]); i++) {
        if (!streqi(mn, alu_ops[i].n)) continue;
        if (opn != 2) break;
        int bits = infer_bits_from_operand(&ops[0], infer_bits_from_operand(&ops[1], 32));
        if ((ops[0].kind == OP_REG || ops[0].kind == OP_MEM) && ops[1].kind == OP_REG) {
            if (!maybe_prefix_16(ctx, bits)) return 0;
            return encode_binop_rm_reg(ctx, (unsigned char)(bits == 8 ? (alu_ops[i].rm_reg - 1) : alu_ops[i].rm_reg), &ops[0], &ops[1]);
        }
        if (ops[0].kind == OP_REG && (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
            if (!maybe_prefix_16(ctx, bits)) return 0;
            return encode_binop_reg_rm(ctx, (unsigned char)(bits == 8 ? (alu_ops[i].reg_rm - 1) : alu_ops[i].reg_rm), &ops[0], &ops[1]);
        }
        if ((ops[0].kind == OP_REG || ops[0].kind == OP_MEM) && (ops[1].kind == OP_IMM || ops[1].kind == OP_LABEL)) {
            if (!resolve_imm(ctx, &ops[1], &imm)) return 0;
            return encode_grp1_imm(ctx, alu_ops[i].grp_ext, &ops[0], imm, bits);
        }
    }

    if (streqi(mn, "test") && opn == 2) {
        int bits = infer_bits_from_operand(&ops[0], infer_bits_from_operand(&ops[1], 32));
        if ((ops[0].kind == OP_REG || ops[0].kind == OP_MEM) && ops[1].kind == OP_REG) {
            if (!maybe_prefix_16(ctx, bits)) return 0;
            if (!emit8(ctx, (bits == 8) ? 0x84 : 0x85)) return 0;
            return emit_rm_operand(ctx, ops[1].reg, &ops[0]);
        }
        if ((ops[0].kind == OP_REG || ops[0].kind == OP_MEM) && (ops[1].kind == OP_IMM || ops[1].kind == OP_LABEL)) {
            if (!resolve_imm(ctx, &ops[1], &imm)) return 0;
            if (bits == 8) return emit8(ctx, 0xF6) && emit_rm_operand(ctx, 0, &ops[0]) && emit8(ctx, (unsigned int)(imm & 0xFF));
            if (!maybe_prefix_16(ctx, bits)) return 0;
            if (!emit8(ctx, 0xF7)) return 0;
            if (!emit_rm_operand(ctx, 0, &ops[0])) return 0;
            if (bits == 16) return emit16(ctx, (unsigned int)imm);
            return emit32(ctx, (unsigned int)imm);
        }
    }

    if ((streqi(mn, "shl") || streqi(mn, "sal") || streqi(mn, "shr") || streqi(mn, "sar") ||
         streqi(mn, "rol") || streqi(mn, "ror")) && opn == 2) {
        int ext = 0;
        if (streqi(mn, "rol")) ext = 0;
        else if (streqi(mn, "ror")) ext = 1;
        else if (streqi(mn, "shl") || streqi(mn, "sal")) ext = 4;
        else if (streqi(mn, "shr")) ext = 5;
        else if (streqi(mn, "sar")) ext = 7;
        int bits = infer_bits_from_operand(&ops[0], 32);
        if (ops[1].kind == OP_REG && ops[1].reg_bits == 8 && ops[1].reg == 1) {
            if (!maybe_prefix_16(ctx, bits)) return 0;
            if (!emit8(ctx, bits == 8 ? 0xD2 : 0xD3)) return 0;
            return emit_rm_operand(ctx, ext, &ops[0]);
        }
        if (ops[1].kind == OP_IMM || ops[1].kind == OP_LABEL) {
            if (!resolve_imm(ctx, &ops[1], &imm)) return 0;
            if (!maybe_prefix_16(ctx, bits)) return 0;
            if (!emit8(ctx, bits == 8 ? 0xC0 : 0xC1)) return 0;
            if (!emit_rm_operand(ctx, ext, &ops[0])) return 0;
            return emit8(ctx, (unsigned int)(imm & 0xFF));
        }
    }

    print_err2("unsupported instruction: ", mn);
    ctx->had_error = 1;
    return 0;
}

static int emit_data_item(asm_ctx_t *ctx, const char *tok, int bytes) {
    char t[MAX_TOK];
    copy_lim(t, tok, MAX_TOK);
    char *s = ltrim(t);
    rtrim(s);

    int n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        for (int i = 1; i < n - 1; i++) {
            char c = s[i];
            if (c == '\\' && i + 1 < n - 1) {
                char e = s[++i];
                if (e == 'n') c = '\n';
                else if (e == 'r') c = '\r';
                else if (e == 't') c = '\t';
                else if (e == '\\') c = '\\';
                else if (e == '"') c = '"';
                else if (e == '0') c = '\0';
                else c = e;
            }
            if (bytes != 1) {
                print_err("string literals only valid in db");
                ctx->had_error = 1;
                return 0;
            }
            if (!emit8(ctx, (unsigned char)c)) return 0;
        }
        return 1;
    }

    int val = 0;
    int ok = parse_int(s, &val);
    if (!ok) {
        if (ctx->fmt_obj && bytes == 4 && ctx->pass == 2) {
            unsigned int off = ctx->sec_pc[ctx->cur_sec];
            if (!add_reloc(ctx, (unsigned int)ctx->cur_sec, off, MOBJ_RELOC_ABS32, s, 0)) return 0;
            val = 0;
            ok = 1;
        }
        // label value
        if (!ok && !resolve_label(ctx, s, &val) && ctx->pass == 1) val = 0;
    }
    if (bytes == 1) return emit8(ctx, (unsigned int)(val & 0xFF));
    if (bytes == 2) return emit16(ctx, (unsigned int)(val & 0xFFFF));
    return emit32(ctx, (unsigned int)val);
}

static int process_line(asm_ctx_t *ctx, char *line) {
    int in_str = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) in_str = !in_str;
        if (!in_str && (line[i] == ';' || line[i] == '#')) {
            line[i] = 0;
            break;
        }
    }

    char *s = ltrim(line);
    rtrim(s);
    if (!s[0]) return 1;
    if (ctx->pass == 1) ctx->meaningful_lines++;

    while (1) {
        char *colon = find_char(s, ':');
        if (!colon) break;
        int valid = 1;
        for (char *p = s; p < colon; p++) {
            if (*p == ' ' || *p == '\t') { valid = 0; break; }
        }
        if (!valid) break;
        char name[MAX_NAME];
        int len = (int)(colon - s);
        if (len <= 0 || len >= MAX_NAME) return 1;
        memcpy(name, s, (unsigned int)len);
        name[len] = 0;
        define_label(ctx, name, ctx->sec_pc[ctx->cur_sec], ctx->cur_sec);
        s = ltrim(colon + 1);
        if (!s[0]) return 1;
    }

    char mnem[MAX_TOK];
    int mi = 0;
    while (s[mi] && s[mi] != ' ' && s[mi] != '\t') mi++;
    if (mi <= 0) return 1;
    if (mi >= MAX_TOK) mi = MAX_TOK - 1;
    memcpy(mnem, s, (unsigned int)mi);
    mnem[mi] = 0;
    char *rest = ltrim(s + mi);

    if (streqi(mnem, "bits")) {
        return 1;
    }

    if (streqi(mnem, "global") || streqi(mnem, "extern")) {
        char toks[64][MAX_TOK];
        int n = split_operands(rest, toks, 64);
        for (int i = 0; i < n; i++) {
            if (!toks[i][0]) continue;
            if (streqi(mnem, "global")) mark_label_global(ctx, toks[i]);
            else mark_label_extern(ctx, toks[i]);
        }
        return 1;
    }

    if (streqi(mnem, "section")) {
        char sec[MAX_TOK];
        copy_lim(sec, rest, MAX_TOK);
        char *p = ltrim(sec);
        rtrim(p);
        if (!p[0]) {
            print_err("missing section name");
            ctx->had_error = 1;
            return 0;
        }
        if (p[0] == '.') p++;
        if (streqi(p, "text")) ctx->cur_sec = SEC_TEXT;
        else if (streqi(p, "rodata")) ctx->cur_sec = SEC_RODATA;
        else if (streqi(p, "data")) ctx->cur_sec = SEC_DATA;
        else if (streqi(p, "bss")) ctx->cur_sec = SEC_BSS;
        else {
            print_err2("unknown section: ", p);
            ctx->had_error = 1;
            return 0;
        }
        return 1;
    }

    if (streqi(mnem, "org")) {
        int v = 0;
        if (!parse_int(rest, &v)) {
            print_err("bad org value");
            ctx->had_error = 1;
            return 0;
        }
        if (ctx->sec_pc[SEC_TEXT] == 0 &&
            ctx->sec_pc[SEC_RODATA] == 0 &&
            ctx->sec_pc[SEC_DATA] == 0 &&
            ctx->sec_pc[SEC_BSS] == 0) {
            ctx->org = (unsigned int)v;
            return 1;
        }
        print_err("org only supported before output");
        ctx->had_error = 1;
        return 0;
    }

    if (streqi(mnem, "align")) {
        int v = 0;
        if (!parse_int(rest, &v) || v <= 0) {
            print_err("bad align value");
            ctx->had_error = 1;
            return 0;
        }
        while ((ctx->sec_pc[ctx->cur_sec] % (unsigned int)v) != 0) {
            if (!emit8(ctx, 0)) return 0;
        }
        return 1;
    }

    if (streqi(mnem, "resb") || streqi(mnem, "resw") || streqi(mnem, "resd")) {
        int count = 0;
        if (!parse_int(rest, &count) || count < 0) {
            print_err("bad res count");
            ctx->had_error = 1;
            return 0;
        }
        int sz = streqi(mnem, "resb") ? 1 : (streqi(mnem, "resw") ? 2 : 4);
        for (int i = 0; i < count * sz; i++) {
            if (!emit8(ctx, 0)) return 0;
        }
        return 1;
    }

    if (streqi(mnem, "times")) {
        char parts[2][MAX_TOK];
        int p = 0;
        int start = 0;
        for (int i = 0; ; i++) {
            char c = rest[i];
            if ((c == ' ' || c == '\t' || c == 0) && i > start) {
                int len = i - start;
                if (len >= MAX_TOK) len = MAX_TOK - 1;
                if (p < 2) {
                    memcpy(parts[p], rest + start, (unsigned int)len);
                    parts[p][len] = 0;
                    p++;
                }
                while (rest[i] == ' ' || rest[i] == '\t') i++;
                start = i;
                if (c == 0 || p == 2) break;
            } else if (c == 0) {
                break;
            }
        }
        if (p < 2) {
            print_err("bad times syntax");
            ctx->had_error = 1;
            return 0;
        }
        int count = 0;
        if (!parse_int(parts[0], &count) || count < 0) {
            print_err("bad times count");
            ctx->had_error = 1;
            return 0;
        }
        char *tail = ltrim(rest + strlen(parts[0]));
        tail = ltrim(tail + strlen(parts[1]));
        int bytes = 0;
        if (streqi(parts[1], "db")) bytes = 1;
        else if (streqi(parts[1], "dw")) bytes = 2;
        else if (streqi(parts[1], "dd")) bytes = 4;
        else {
            print_err("times supports only db/dw/dd");
            ctx->had_error = 1;
            return 0;
        }
        for (int i = 0; i < count; i++) {
            if (!emit_data_item(ctx, tail, bytes)) return 0;
        }
        return 1;
    }

    if (streqi(mnem, "db") || streqi(mnem, "dw") || streqi(mnem, "dd")) {
        int bytes = streqi(mnem, "db") ? 1 : (streqi(mnem, "dw") ? 2 : 4);
        char toks[64][MAX_TOK];
        int n = split_operands(rest, toks, 64);
        for (int i = 0; i < n; i++) {
            if (!emit_data_item(ctx, toks[i], bytes)) return 0;
        }
        return 1;
    }

    operand_t ops[3];
    memset(ops, 0, sizeof(ops));
    int opn = 0;
    if (rest[0]) {
        char toks[3][MAX_TOK];
        opn = split_operands(rest, toks, 3);
        for (int i = 0; i < opn; i++) {
            if (!parse_operand(toks[i], &ops[i])) {
                print_err("bad operand");
                ctx->had_error = 1;
                return 0;
            }
        }
    }

    return encode_instruction(ctx, mnem, ops, opn);
}

static int run_pass(asm_ctx_t *ctx, int pass) {
    char line[MAX_LINE];
    ctx->pass = pass;
    ctx->cur_sec = SEC_TEXT;
    memset(ctx->sec_pc, 0, sizeof(ctx->sec_pc));
    if (pass == 2) {
        memset(ctx->sec_len, 0, sizeof(ctx->sec_len));
        ctx->reloc_count = 0;
    }

    int p = 0;
    ctx->line_no = 0;
    while (p < ctx->src_len) {
        int li = 0;
        while (p < ctx->src_len && ctx->src[p] != '\n' && li < MAX_LINE - 1) line[li++] = ctx->src[p++];
        if (p < ctx->src_len && ctx->src[p] == '\n') p++;
        line[li] = 0;
        ctx->line_no++;
        copy_lim(ctx->cur_line, line, MAX_LINE);
        if (!process_line(ctx, line)) return 0;
        if (ctx->had_error) return 0;
    }
    return !ctx->had_error;
}

static int assemble(asm_ctx_t *ctx) {
    char line[MAX_LINE];
    (void)line;

    ctx->label_count = 0;
    ctx->meaningful_lines = 0;
    if (!run_pass(ctx, 1)) {
        if (ctx->line_no > 0) {
            print("as86: error at line ");
            print_num(ctx->line_no);
            print(": ");
            print(ctx->cur_line);
            print("\n");
        }
        return 0;
    }

    ctx->sec_size[SEC_TEXT] = ctx->sec_pc[SEC_TEXT];
    ctx->sec_size[SEC_RODATA] = ctx->sec_pc[SEC_RODATA];
    ctx->sec_size[SEC_DATA] = ctx->sec_pc[SEC_DATA];
    ctx->sec_size[SEC_BSS] = ctx->sec_pc[SEC_BSS];

    ctx->sec_base[SEC_TEXT] = 0;
    ctx->sec_base[SEC_RODATA] = ctx->sec_base[SEC_TEXT] + ctx->sec_size[SEC_TEXT];
    ctx->sec_base[SEC_DATA] = ctx->sec_base[SEC_RODATA] + ctx->sec_size[SEC_RODATA];
    ctx->sec_base[SEC_BSS] = ctx->sec_base[SEC_DATA] + ctx->sec_size[SEC_DATA];

    int need = (int)(ctx->sec_size[SEC_TEXT] + ctx->sec_size[SEC_RODATA] +
                     ctx->sec_size[SEC_DATA] + ctx->sec_size[SEC_BSS]);
    if (need == 0 && ctx->meaningful_lines > 0) {
        print_err("no encodable output (0 bytes); likely unsupported asm forms");
        ctx->had_error = 1;
        return 0;
    }
    for (int s = 0; s < SEC_COUNT; s++) {
        int cap = (int)ctx->sec_size[s];
        if (cap <= 0) cap = 1;
        ctx->sec_cap[s] = cap;
        ctx->sec_out[s] = (unsigned char *)malloc((unsigned int)cap);
        if (!ctx->sec_out[s]) {
            print_err("out of memory (section buffer)");
            ctx->had_error = 1;
            return 0;
        }
        memset(ctx->sec_out[s], 0, (unsigned int)cap);
    }

    if (!run_pass(ctx, 2)) {
        if (ctx->line_no > 0) {
            print("as86: error at line ");
            print_num(ctx->line_no);
            print(": ");
            print(ctx->cur_line);
            print("\n");
        }
        return 0;
    }
    ctx->out = (unsigned char *)malloc((unsigned int)(need > 0 ? need : 1));
    if (!ctx->out) {
        print_err("out of memory (final output)");
        ctx->had_error = 1;
        return 0;
    }
    ctx->out_len = 0;
    for (int s = 0; s < SEC_COUNT; s++) {
        if (ctx->sec_len[s] > 0) {
            memcpy(ctx->out + ctx->out_len, ctx->sec_out[s], (unsigned int)ctx->sec_len[s]);
            ctx->out_len += ctx->sec_len[s];
        }
    }
    return !ctx->had_error;
}

static void usage(void) {
    print("usage: as86 [-f bin|obj] [--org addr] [-o out] <input.asm> [output]\n");
    print("phase-1: 32-bit flat binary assembler (subset)\n");
}

void _start(int argc, char **argv) {
    const char *in = 0;
    const char *out = 0;
    int fmt_obj = 0;
    unsigned int org_cli = 0;
    int have_org_cli = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (streq(a, "-o")) {
            if (i + 1 >= argc) { usage(); exit(1); }
            out = argv[++i];
            continue;
        }
        if (streq(a, "-f")) {
            if (i + 1 >= argc) { usage(); exit(1); }
            const char *fmt = argv[++i];
            if (streqi(fmt, "bin")) {
                fmt_obj = 0;
            } else if (streqi(fmt, "obj")) {
                fmt_obj = 1;
            } else {
                print_err("only -f bin or -f obj is supported");
                exit(1);
            }
            continue;
        }
        if (streq(a, "--org") || streq(a, "-org")) {
            if (i + 1 >= argc) { usage(); exit(1); }
            int v = 0;
            if (!parse_int(argv[++i], &v) || v < 0) {
                print_err("bad --org value");
                exit(1);
            }
            org_cli = (unsigned int)v;
            have_org_cli = 1;
            continue;
        }
        if (a[0] == '-') {
            print_err2("unknown option: ", a);
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

    int fd = open(in, O_RDONLY);
    if (fd < 0) {
        print_err2("cannot open input: ", in);
        exit(1);
    }

    char *src = (char *)malloc(MAX_SRC);
    if (!src) {
        close(fd);
        print_err("out of memory");
        exit(1);
    }
    int n = fread(fd, src, MAX_SRC - 1);
    close(fd);
    if (n < 0) {
        print_err("read failed");
        exit(1);
    }
    src[n] = 0;

    asm_ctx_t *ctx = (asm_ctx_t *)malloc(sizeof(asm_ctx_t));
    if (!ctx) {
        print_err("out of memory");
        exit(1);
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->src = src;
    ctx->src_len = n;
    ctx->org = have_org_cli ? org_cli : 0;
    ctx->fmt_obj = fmt_obj;

    if (!assemble(ctx)) exit(1);

    int ofd = open(out, O_WRONLY | O_CREAT | O_TRUNC);
    if (ofd < 0) {
        print_err2("cannot open output: ", out);
        exit(1);
    }
    if (!fmt_obj) {
        if (fwrite(ofd, ctx->out, (unsigned int)ctx->out_len) != ctx->out_len) {
            close(ofd);
            print_err("write failed");
            exit(1);
        }
    } else {
        mobj_header_t h;
        memset(&h, 0, sizeof(h));
        h.magic[0] = 'M'; h.magic[1] = 'O'; h.magic[2] = 'B'; h.magic[3] = 'J';
        h.version = 2;
        h.org = ctx->org;
        h.text_size = ctx->sec_len[SEC_TEXT];
        h.rodata_size = ctx->sec_len[SEC_RODATA];
        h.data_size = ctx->sec_len[SEC_DATA];
        h.bss_size = ctx->sec_len[SEC_BSS];
        h.sym_count = (unsigned int)ctx->label_count;
        h.reloc_count = (unsigned int)ctx->reloc_count;
        h.entry_off = 0;
        unsigned int ent = 0;
        if (find_label_addr(ctx, "$_start", &ent) || find_label_addr(ctx, "_start", &ent)) {
            h.entry_off = ent - ctx->org;
        }

        if (fwrite(ofd, &h, sizeof(h)) != (int)sizeof(h)) {
            close(ofd);
            print_err("write failed");
            exit(1);
        }
        if (ctx->sec_len[SEC_TEXT] &&
            fwrite(ofd, ctx->sec_out[SEC_TEXT], (unsigned int)ctx->sec_len[SEC_TEXT]) != ctx->sec_len[SEC_TEXT]) {
            close(ofd);
            print_err("write failed");
            exit(1);
        }
        if (ctx->sec_len[SEC_RODATA] &&
            fwrite(ofd, ctx->sec_out[SEC_RODATA], (unsigned int)ctx->sec_len[SEC_RODATA]) != ctx->sec_len[SEC_RODATA]) {
            close(ofd);
            print_err("write failed");
            exit(1);
        }
        if (ctx->sec_len[SEC_DATA] &&
            fwrite(ofd, ctx->sec_out[SEC_DATA], (unsigned int)ctx->sec_len[SEC_DATA]) != ctx->sec_len[SEC_DATA]) {
            close(ofd);
            print_err("write failed");
            exit(1);
        }
        for (int i = 0; i < ctx->label_count; i++) {
            mobj_sym_t s;
            memset(&s, 0, sizeof(s));
            copy_lim(s.name, ctx->labels[i].name, MAX_NAME);
            if (ctx->labels[i].defined) {
                s.value_off = ctx->labels[i].offset;
                s.section = (unsigned int)ctx->labels[i].section;
            } else {
                s.value_off = 0;
                s.section = SEC_UNDEF;
            }
            if (ctx->labels[i].is_global) s.flags |= MOBJ_SYM_GLOBAL;
            if (ctx->labels[i].is_extern || !ctx->labels[i].defined) s.flags |= MOBJ_SYM_EXTERN;
            if (fwrite(ofd, &s, sizeof(s)) != (int)sizeof(s)) {
                close(ofd);
                print_err("write failed");
                exit(1);
            }
        }
        for (int i = 0; i < ctx->reloc_count; i++) {
            if (fwrite(ofd, &ctx->relocs[i], sizeof(ctx->relocs[i])) != (int)sizeof(ctx->relocs[i])) {
                close(ofd);
                print_err("write failed");
                exit(1);
            }
        }
    }
    close(ofd);

    print("as86: wrote ");
    if (!fmt_obj) {
        print_num(ctx->out_len);
    } else {
        int total = (int)sizeof(mobj_header_t) + ctx->sec_len[SEC_TEXT] + ctx->sec_len[SEC_RODATA] + ctx->sec_len[SEC_DATA]
                  + (int)sizeof(mobj_sym_t) * ctx->label_count + (int)sizeof(mobj_reloc_t) * ctx->reloc_count;
        print_num(total);
    }
    print(" bytes to ");
    print(out);
    print("\n");
    exit(0);
}
