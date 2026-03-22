/*
 * boot.S — x86_64 multiboot2 entry and long-mode bring-up
 *
 * Flow (all 32-bit protected-mode code until the far jump to 64-bit):
 *   1. Multiboot2 header (must be within first 32KB of image)
 *   2. Check multiboot2 magic (eax == 0x36d76289)
 *   3. Enable PAE (CR4.PAE)
 *   4. Build minimal 4-level page tables in BSS (identity + higher-half)
 *   5. Load CR3 with physical address of PML4
 *   6. Set EFER.LME (long mode enable) via MSR 0xC0000080
 *   7. Enable paging + protected mode (CR0.PG | CR0.PE) → activates long mode
 *   8. Far-jump through a 64-bit GDT descriptor to flush CS pipeline
 *   9. Set up 64-bit segment registers, switch to virtual-address stack
 *  10. Call kernel_main(magic, mbi)
 *
 * Memory layout used here:
 *   VMA 0xC0200000 = LMA 0x00200000  (same offset 0xC0000000 as 32-bit kernel)
 *   Higher-half base: 0xC0000000 (fits in 32 bits — simplifies cross-arch compat)
 *   Identity map: PA 0–8MB also at VA 0–8MB (removed after init)
 *   Higher-half: PA 0–8MB at VA 0xC0000000–0xC0800000
 *
 * 4-level paging at boot maps 2MB superpages (PDE with PS bit) to avoid
 * needing page tables at the PT level during boot.
 *
 * Registers on multiboot2 entry (from bootloader / QEMU -kernel):
 *   EAX = 0x36d76289  (multiboot2 magic)
 *   EBX = physical pointer to multiboot2 info structure
 */

/* ── Multiboot2 header ───────────────────────────────────────────────── */
.set MB2_MAGIC,     0xE85250D6
.set MB2_ARCH,      0            /* i386 protected-mode entry */
.set MB2_HDR_LEN,   (mb2_header_end - mb2_header_start)
.set MB2_CHECKSUM,  -(MB2_MAGIC + MB2_ARCH + MB2_HDR_LEN)
.set MB2_ENTRY_MAGIC, 0x36d76289

/* Higher-half offset used throughout this file */
.set KVBASE,        0xC0000000

/* ── BSS page-table structures (4KB-aligned, zeroed by loader) ───────── */
.section .bss
.align 4096
boot_pml4:        .skip 4096   /* PML4: 512 × 8-byte entries                */
boot_pdpt_lo:     .skip 4096   /* PDPT for identity map (VA 0–1GB)          */
boot_pdpt_hi:     .skip 4096   /* PDPT for higher-half (VA 0xC0000000+)     */
boot_pd_lo:       .skip 4096   /* PD for identity 0–1GB (2MB superpages)    */
boot_pd_hi:       .skip 4096   /* PD for higher-half (2MB superpages)       */

.align 16
.global stack_bottom
stack_bottom:     .skip 16384  /* 16 KiB boot stack */
.global stack_top
stack_top:

/* ── Multiboot2 header section (must precede .boot in the image) ─────── */
.section .multiboot2
.align 8
mb2_header_start:
    .long  MB2_MAGIC
    .long  MB2_ARCH
    .long  MB2_HDR_LEN
    .long  MB2_CHECKSUM
    /* terminating tag */
    .word  0    /* type = end */
    .word  0    /* flags */
    .long  8    /* size */
mb2_header_end:

/* ── 32-bit protected-mode entry (paging OFF, 32-bit CS) ─────────────── */
.section .boot
.code32
.global _start
.type _start, @function
_start:
    cli

    /* Save multiboot2 info (EAX=magic, EBX=mbi pointer) */
    mov %eax, %esi   /* esi = MB2 magic  */
    mov %ebx, %edi   /* edi = mbi phys ptr */

    /* Physical stack before paging is enabled */
    mov $(stack_top - KVBASE), %esp

    /* ── Verify multiboot2 magic ── */
    cmp $MB2_ENTRY_MAGIC, %esi
    jne .Lhang        /* wrong bootloader — halt */

    /* ── Enable PAE (CR4[5]) ── */
    mov %cr4, %eax
    or  $0x20, %eax
    mov %eax, %cr4

    /* ── Build PD for identity-map (0–8MB via 2MB superpages) ── */
    /* PDE flags: PS(bit7)=1, RW(bit1)=1, P(bit0)=1 → 0x83 */
    mov $(boot_pd_lo - KVBASE), %ebx
    movl $0x0000083, 0(%ebx)     /* 0–2MB   */
    movl $0x0200083, 8(%ebx)     /* 2–4MB   */
    movl $0x0400083, 16(%ebx)    /* 4–6MB   */
    movl $0x0600083, 24(%ebx)    /* 6–8MB   */

    /* ── Build PD for higher-half (same 0–8MB) ── */
    mov $(boot_pd_hi - KVBASE), %ebx
    movl $0x0000083, 0(%ebx)
    movl $0x0200083, 8(%ebx)
    movl $0x0400083, 16(%ebx)
    movl $0x0600083, 24(%ebx)

    /* ── Wire PDPT_LO → PD_LO (entry 0: covers VA 0–1GB) ── */
    /* PDPTE flags: RW | P = 0x3 */
    mov $(boot_pdpt_lo - KVBASE), %ebx
    mov $(boot_pd_lo - KVBASE), %eax
    or  $0x3, %eax
    movl %eax, 0(%ebx)    /* low 32 bits */
    movl $0,   4(%ebx)    /* high 32 bits */

    /* ── Wire PDPT_HI → PD_HI ── */
    /* KVBASE >> 30 = 3, so VA 0xC0000000 is PDPT entry 3 in the 4th GB */
    /* Actually KVBASE=0xC0000000, PML4 index=0, PDPT index=(0xC0000000>>30)=3 */
    mov $(boot_pdpt_hi - KVBASE), %ebx
    mov $(boot_pd_hi - KVBASE), %eax
    or  $0x3, %eax
    movl %eax, (3*8)(%ebx)   /* entry 3 (covers 0xC0000000–0xFFFFFFFF) */
    movl $0,   (3*8+4)(%ebx)

    /* ── Wire PML4 → both PDPTs ── */
    /* Both identity and higher-half share PML4 entry 0 (VA bits[47:39] = 0) */
    /* because 0xC0000000 only uses bits[31:0] — fits within PML4 entry 0's range */
    mov $(boot_pml4 - KVBASE), %ebx
    /* entry 0 → pdpt_lo */
    mov $(boot_pdpt_lo - KVBASE), %eax
    or  $0x3, %eax
    movl %eax, 0(%ebx)
    movl $0,   4(%ebx)
    /* also point to pdpt_hi at entry 0 (it covers the upper 1GB of the 4GB space) */
    /* pdpt_hi is accessed through the same PML4 entry 0, different PDPT entry 3 */
    /* Wire pdpt_hi into PML4 entry 0 as well — BUT pdpt_lo is already there.    */
    /* Solution: put BOTH mappings in a single merged PDPT_LO.                    */
    /* Re-do: use only boot_pdpt_lo, add entry 3 there for higher-half.           */
    mov $(boot_pdpt_lo - KVBASE), %ebx
    mov $(boot_pd_hi - KVBASE), %eax
    or  $0x3, %eax
    movl %eax, (3*8)(%ebx)
    movl $0,   (3*8+4)(%ebx)

    /* ── Load CR3 ── */
    mov $(boot_pml4 - KVBASE), %eax
    mov %eax, %cr3

    /* ── Enable long mode: set EFER.LME (MSR 0xC0000080) ── */
    mov $0xC0000080, %ecx
    rdmsr
    or  $0x100, %eax    /* bit 8 = LME */
    wrmsr

    /* ── Enable paging + keep PE (activates long-mode compatibility) ── */
    mov %cr0, %eax
    or  $0x80000001, %eax   /* PG | PE */
    mov %eax, %cr0

    /* ── Far jump into 64-bit code segment to flush CS ── */
    /* We need a temporary GDT with a 64-bit code descriptor. */
    lgdt (boot_gdt_ptr - KVBASE)
    /* 0x08 = kernel code descriptor (index 1, RPL 0) */
    ljmp $0x08, $(long_mode_entry - KVBASE)

.Lhang:
    hlt
    jmp .Lhang

/* ── Temporary GDT for the far jump (minimal: null + 64-bit code) ─────── */
.align 8
boot_gdt:
    /* Null descriptor */
    .quad 0
    /* 64-bit code: L=1(bit53), P=1, DPL=0, S=1(code/data), type=0xA(exec+read) */
    /* Limit/base ignored in long mode — only L and P matter */
    .quad 0x0020980000000000
boot_gdt_end:

boot_gdt_ptr:
    .word (boot_gdt_end - boot_gdt - 1)
    .long (boot_gdt - KVBASE)   /* physical address (32-bit OK at this point) */

/* ── 64-bit entry point ────────────────────────────────────────────────── */
.code64
.section .text
.global long_mode_entry
long_mode_entry:
    /* Zero out data segment registers (not used in 64-bit kernel mode) */
    xor %eax, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    /* Switch to the higher-half virtual stack address.
     * Use movabs to get a 64-bit immediate (stack_top VMA > 2GB for 32S reloc). */
    movabs $stack_top, %rax
    mov %rax, %rsp

    /* Clear frame pointer for stack traces */
    xor %rbp, %rbp

    /* Call kernel_main(magic, mbi_phys)
     * 64-bit SysV ABI: first two args in rdi, rsi.
     * esi/edi were set before the far jump (32-bit regs, zero-extended in 64-bit). */
    movl %edi, %edi   /* zero-extend mbi ptr  */
    movl %esi, %esi   /* zero-extend magic    */
    /* Swap: kernel_main(magic, mbi) */
    mov %rsi, %rdi    /* rdi = magic */
    mov %rdi, %rsi    /* rsi = mbi   */
    /* Actually: esi=magic, edi=mbi from 32-bit setup — restore correctly */
    /* We stored: esi=magic(eax), edi=mbi(ebx) before the far jump */
    /* After far jump: rsi=0-extended magic, rdi=0-extended mbi */
    /* kernel_main expects (magic, mbi) → rdi=magic, rsi=mbi */
    /* rsi currently has magic (was %esi), rdi has mbi */
    /* Swap them: */
    mov %rdi, %rax    /* rax = mbi  */
    mov %rsi, %rdi    /* rdi = magic */
    mov %rax, %rsi    /* rsi = mbi  */

    call kernel_main

    /* Should not return */
.Lhalt64:
    hlt
    jmp .Lhalt64
