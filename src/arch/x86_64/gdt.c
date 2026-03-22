/*
 * gdt.c — 64-bit GDT setup
 *
 * Each normal descriptor is 8 bytes.  The 64-bit TSS descriptor is 16 bytes
 * (two consecutive slots), needed because the TSS base is a full 64-bit pointer.
 *
 * Long-mode descriptor access byte (byte 5):
 *   P DPL[2] S  Type[4]
 *   where Type for code = 1010b (execute/read), data = 0010b (read/write)
 *
 * Flags nibble (byte 6 high nibble):
 *   G AVL L DB  — G=1 (4KB granularity), L=1 (64-bit), DB=0 (must be 0 in 64-bit mode)
 *   For data: G=1 L=0 DB=1 (same as 32-bit data segment — still works in 64-bit mode)
 */
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/tss.h"
#include "lib.h"

/* Opaque 64-bit GDT entry */
typedef uint64_t gdt_entry64_t;

/* TSS descriptor: 16 bytes (two consecutive 64-bit slots) */
typedef struct {
    uint64_t lo;
    uint64_t hi;
} __attribute__((packed)) tss_descriptor_t;

/* GDT: null + kcode + kdata + ucode + udata + TSS (16 bytes = 2 slots) */
static struct {
    gdt_entry64_t null;
    gdt_entry64_t kcode;
    gdt_entry64_t kdata;
    gdt_entry64_t ucode;
    gdt_entry64_t udata;
    tss_descriptor_t tss;
} __attribute__((packed)) gdt64;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr64_t;

static gdtr64_t gdtr64;

/* Build a simple 64-bit code/data segment descriptor.
 * In long mode, base and limit are ignored for code/data,
 * but we fill them for completeness. */
static gdt_entry64_t make_seg(int dpl, int is_code, int is_64bit) {
    /* Access byte: P=1, DPL, S=1, type */
    uint8_t access = 0x90 | ((dpl & 3) << 5);
    if (is_code)
        access |= 0x0A;  /* execute/read */
    else
        access |= 0x02;  /* read/write */

    /* Flags nibble (byte 6 upper nibble): G | AVL | L | DB */
    uint8_t flags;
    if (is_64bit && is_code)
        flags = 0xA;  /* G=1, L=1, DB=0 — 64-bit code */
    else
        flags = 0xC;  /* G=1, L=0, DB=1 — 32-bit data (works in 64-bit mode) */

    gdt_entry64_t e = 0;
    /* limit low 16 bits */
    e |= 0xFFFF;
    /* access byte at bits [40:47] */
    e |= ((uint64_t)access) << 40;
    /* flags + limit high at bits [48:55] */
    e |= ((uint64_t)(flags << 4 | 0xF)) << 48;
    return e;
}

/* Build the 16-byte 64-bit TSS descriptor.
 * Type = 0x9 (available 64-bit TSS), P=1, DPL=0. */
static void make_tss_descriptor(tss_descriptor_t *d, uintptr_t base, uint32_t limit) {
    d->lo = 0;
    d->hi = 0;

    /* limit[15:0] */
    d->lo |= (uint64_t)(limit & 0xFFFF);
    /* base[15:0] at [16:31] */
    d->lo |= ((uint64_t)(base & 0xFFFF)) << 16;
    /* base[23:16] at [32:39] */
    d->lo |= ((uint64_t)((base >> 16) & 0xFF)) << 32;
    /* access: P=1, DPL=0, type=0x9 (avail TSS) at [40:47] */
    d->lo |= ((uint64_t)0x89) << 40;
    /* limit[19:16] + flags at [48:55]: G=0 (byte granularity for TSS) */
    d->lo |= ((uint64_t)((limit >> 16) & 0xF)) << 48;
    /* base[31:24] at [56:63] */
    d->lo |= ((uint64_t)((base >> 24) & 0xFF)) << 56;
    /* upper 64 bits: base[63:32] in bits [0:31] */
    d->hi = (uint64_t)(base >> 32);
}

/* Defined in gdt_asm.S */
extern void gdt64_flush(uint64_t gdtr_addr, uint16_t cs, uint16_t ds);

void gdt64_init(void) {
    gdt64.null  = 0;
    gdt64.kcode = make_seg(0, 1, 1);   /* kernel code, 64-bit */
    gdt64.kdata = make_seg(0, 0, 0);   /* kernel data */
    gdt64.ucode = make_seg(3, 1, 1);   /* user code, 64-bit */
    gdt64.udata = make_seg(3, 0, 0);   /* user data */

    /* TSS descriptor */
    tss64_t *tss = tss64_get();
    make_tss_descriptor(&gdt64.tss,
                        (uintptr_t)tss,
                        sizeof(tss64_t) - 1);

    gdtr64.limit = sizeof(gdt64) - 1;
    gdtr64.base  = (uint64_t)&gdt64;

    /* Load GDTR and reload CS/DS via far return in assembly */
    gdt64_flush((uint64_t)&gdtr64, GDT64_KERNEL_CODE, GDT64_KERNEL_DATA);

    /* Load TSS */
    __asm__ volatile("ltr %0" :: "r"((uint16_t)GDT64_TSS_SEL));
}
