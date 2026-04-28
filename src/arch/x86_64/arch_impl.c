/*
 * arch_impl.c — x86_64 implementation of arch_interface.h
 *
 * Thin wrappers over the real x86_64 subsystems (paging.c, tss.c, etc.).
 * Mirrors the i686 arch_impl.c structure.
 *
 * Concrete type definitions:
 *   arch_aspace_t    — wraps pml4_t (4-level page directory)
 *   arch_irq_frame_t — the CPU iret frame (rip/cs/rflags/rsp/ss)
 *                      located at the TOP of the cpu_state_t pushed by stubs
 */
#include "arch/arch_interface.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/x86_64init.h"
#include "memlayout.h"
#include "lib.h"

/* ── Concrete opaque types ──────────────────────────────────────────────── */

struct arch_aspace {
    pml4_t pml4;   /* 4-level page map level 4 table */
};

/*
 * 64-bit iret frame — the five words at the TOP of the cpu_state_t
 * pushed by the ISR stub (just below the error_code).
 * arch_irq_frame_t * is cast from (cpu_state_t * + offsetof(error_code) + 8).
 */
struct arch_irq_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* ── Constants ──────────────────────────────────────────────────────────── */

/* Default RFLAGS: IF(9) set, IOPL=0, all others 0 */
#define ARCH_RFLAGS_DEFAULT  0x202ULL

/* ── Address space ──────────────────────────────────────────────────────── */

arch_aspace_t *arch_aspace_create(void) {
    pml4_t *pml4 = paging64_create_address_space();
    return (arch_aspace_t *)pml4;
}

void arch_aspace_destroy(arch_aspace_t *as) {
    if (as)
        paging64_destroy_address_space((pml4_t *)as);
}

void arch_aspace_switch(arch_aspace_t *as) {
    if (as)
        paging64_switch((pml4_t *)as);
}

arch_aspace_t *arch_aspace_kernel(void) {
    return (arch_aspace_t *)paging64_get_kernel_pml4();
}

int arch_aspace_map(arch_aspace_t *as, uintptr_t vaddr, uintptr_t paddr,
                    uint32_t flags) {
    uint64_t hw_flags = 0;
    if (flags & ARCH_PAGE_PRESENT) hw_flags |= PAGE_PRESENT;
    if (flags & ARCH_PAGE_WRITE)   hw_flags |= PAGE_WRITE;
    if (flags & ARCH_PAGE_USER)    hw_flags |= PAGE_USER;
    return paging64_map_page((pml4_t *)as, vaddr, paddr, hw_flags);
}

void arch_aspace_unmap(arch_aspace_t *as, uintptr_t vaddr) {
    paging64_unmap_page((pml4_t *)as, vaddr);
}

void arch_aspace_map_mmio(uintptr_t phys, uint32_t size) {
    paging64_map_mmio(phys, size);
}

uintptr_t arch_aspace_user_phys(arch_aspace_t *as, uintptr_t vaddr) {
    return paging64_user_phys((pml4_t *)as, vaddr);
}

/* ── Task stack initialisation ──────────────────────────────────────────── */

/*
 * Build the initial kernel-task context frame so schedule() can do a first
 * context switch.  The frame must match what irq0_task / yield_task restore:
 *
 *   (high address = stack_top)
 *   ...
 *   [iretq frame: SS | RSP | RFLAGS | CS | RIP]  (5 × 8 bytes)
 *   [error_code = 0]
 *   [GPR block: r15..rax]                          (15 × 8 bytes)
 *   (low address = new RSP)
 *
 * For a kernel task there is no ring transition, so RSP/SS in the iretq frame
 * are not used.  We still push them because the restore macro pops them.
 * The kernel code segment is GDT64_KERNEL_CODE (0x08).
 */
uint32_t *arch_task_init_kernel(void (*entry)(void), uint32_t *stack_top) {
    uint64_t *sp = (uint64_t *)stack_top;

    /* iretq frame (kernel mode — no ring transition) */
    *(--sp) = GDT64_KERNEL_DATA;          /* SS     (not used by CPU but we pop it) */
    *(--sp) = (uint64_t)(uintptr_t)sp;    /* RSP    (dummy — will be overwritten) */
    *(--sp) = ARCH_RFLAGS_DEFAULT;        /* RFLAGS */
    *(--sp) = GDT64_KERNEL_CODE;          /* CS     */
    *(--sp) = (uint64_t)(uintptr_t)entry; /* RIP    */

    /* error_code placeholder */
    *(--sp) = 0;

    /* GPR block (all zero for fresh task; order matches RESTORE_ALL pop order) */
    /* RESTORE_ALL pops: r15, r14, r13, r12, r11, r10, r9, r8,
     *                   rdi, rsi, rbp, rbx, rdx, rcx, rax  */
    for (int i = 0; i < 15; i++)
        *(--sp) = 0;

    return (uint32_t *)sp;
}

/*
 * Build the initial kernel stack frame for a user-mode task.
 * The iretq frame uses the user CS/SS selectors so CPU switches ring on iretq.
 */
uint32_t *arch_task_init_user(uintptr_t elf_entry, uintptr_t user_esp,
                              uint32_t *kstack_top) {
    uint64_t *sp = (uint64_t *)kstack_top;

    /* iretq frame with ring transition */
    *(--sp) = GDT64_USER_DATA | 3;         /* SS     (ring 3) */
    *(--sp) = (uint64_t)user_esp;          /* RSP    (user stack) */
    *(--sp) = ARCH_RFLAGS_DEFAULT;         /* RFLAGS */
    *(--sp) = GDT64_USER_CODE | 3;         /* CS     (ring 3) */
    *(--sp) = (uint64_t)elf_entry;         /* RIP    */

    /* error_code placeholder */
    *(--sp) = 0;

    /* GPR block — all zero */
    for (int i = 0; i < 15; i++)
        *(--sp) = 0;

    return (uint32_t *)sp;
}

void arch_task_set_kernel_stack(uintptr_t kstack_top) {
    tss64_set_rsp0(kstack_top);
}

/* ── Exec redirect ──────────────────────────────────────────────────────── */

void arch_set_return_context(arch_irq_frame_t *frame, uintptr_t elf_entry,
                             uintptr_t user_esp) {
    frame->rip    = elf_entry;
    frame->cs     = GDT64_USER_CODE | 3;
    frame->rflags = ARCH_RFLAGS_DEFAULT;
    frame->rsp    = user_esp;
    frame->ss     = GDT64_USER_DATA | 3;
}

/* ── Syscall glue ───────────────────────────────────────────────────────── */

/*
 * On x86_64 the syscall args are passed in registers (rax/rdi/rsi/rdx) and
 * unpacked directly in isr128 before calling syscall_handler.  These functions
 * are provided for interface completeness but are not exercised in this path.
 */
void arch_syscall_args_from_state(void *state, arch_syscall_args_t *out) {
    /* cpu_state_t layout (from interrupts.h):
     * offset 14*8 = 112 → rax (syscall number)
     * offset  8*8 =  64 → rdi (arg1)
     * offset  9*8 =  72 → rsi (arg2)  [wait — struct order is r15..rax]
     * Actually order: r15(0),r14(8),..,r8(56), rdi(64),rsi(72),rbp(80),rbx(88),rdx(96),rcx(104),rax(112)
     */
    typedef struct {
        uint64_t r15,r14,r13,r12,r11,r10,r9,r8;
        uint64_t rdi,rsi,rbp,rbx,rdx,rcx,rax;
        uint64_t error_code,rip,cs,rflags,rsp,ss;
    } cs_t;
    cs_t *cs = (cs_t *)state;
    if (out) {
        out->num  = (uintptr_t)cs->rax;
        out->arg1 = (uintptr_t)cs->rdi;
        out->arg2 = (uintptr_t)cs->rsi;
        out->arg3 = (uintptr_t)cs->rdx;
    }
}

void arch_syscall_set_retval(void *state, uintptr_t retval) {
    typedef struct {
        uint64_t r15,r14,r13,r12,r11,r10,r9,r8;
        uint64_t rdi,rsi,rbp,rbx,rdx,rcx,rax;
    } gpr_t;
    ((gpr_t *)state)->rax = retval;
}

arch_irq_frame_t *arch_irq_frame_from_state(void *state) {
    /*
     * state → cpu_state_t base address.
     * iret frame starts at offset 16*8 = 128 (past 15 GPRs + error_code).
     */
    return (arch_irq_frame_t *)((uint8_t *)state + 128);
}

/* ── Architecture initialisation ────────────────────────────────────────── */

void arch_init(void) {
    init_x86_64();
}

void arch_paging_init(void) {
    /* Build the permanent kernel PML4 now that PMM is available. */
    init_paging();
}

/* ── Halt / shutdown ────────────────────────────────────────────────────── */

void __attribute__((noreturn)) arch_halt_forever(void) {
    while (1)
        __asm__ volatile("hlt");
}

/* ── Console output ─────────────────────────────────────────────────────── */

void arch_console_putchar(char c) {
    serial_putchar(c);
}

/* ── CPU information ────────────────────────────────────────────────────── */

void arch_cpu_get_info(arch_cpu_info_t *out) {
    if (!out)
        return;
    /* CPUID is available in 64-bit mode; provide a minimal implementation. */
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    /* leaf 0: max_leaf and vendor string */
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0));
    out->max_leaf = eax;
    /* Vendor string: EBX EDX ECX in order (12 chars + NUL). */
    uint32_t *v = (uint32_t *)out->vendor;
    v[0] = ebx; v[1] = edx; v[2] = ecx;
    out->vendor[12] = '\0';
    /* leaf 1: family/model/stepping and feature flags */
    if (eax >= 1) {
        __asm__ volatile("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(1));
        out->stepping    = (eax >> 0)  & 0xF;
        out->model       = (eax >> 4)  & 0xF;
        out->family      = (eax >> 8)  & 0xF;
        out->feature_ecx = ecx;
        out->feature_edx = edx;
    } else {
        out->stepping = out->model = out->family = 0;
        out->feature_ecx = out->feature_edx = 0;
    }
}

/* ── PCI bus ────────────────────────────────────────────────────────────── */

void arch_pci_init(void) {
    /* TODO: implement PCI for x86_64 */
}

int arch_pci_get_devices(arch_pci_device_t *out, int max) {
    (void)out; (void)max;
    return 0;
}

/* ── Graphics / framebuffer ─────────────────────────────────────────────── */

int      arch_gfx_bga_available(void)                       { return 0; }
uint32_t arch_gfx_enter_bga(int w, int h, int bpp)          { (void)w;(void)h;(void)bpp; return 0; }
void     arch_gfx_exit_bga(void)                            {}
void     arch_gfx_enter_mode13h(void)                       {}
void     arch_gfx_enter_text_mode(void)                     {}
uint32_t arch_gfx_mode13h_fb_start(void)                    { return 0xA0000u; }
uint32_t arch_gfx_mode13h_fb_end(void)                      { return 0xB0000u; }

/* ── PS/2 mouse ─────────────────────────────────────────────────────────── */

void arch_mouse_init(void)                                   {}
void arch_mouse_irq_handler(uintptr_t irq, uintptr_t vec)   { (void)irq;(void)vec; }
void arch_mouse_get_state(int *x, int *y, uint8_t *buttons) {
    if (x)       *x       = 0;
    if (y)       *y       = 0;
    if (buttons) *buttons = 0;
}
void arch_mouse_set_bounds(int w, int h)                     { (void)w;(void)h; }

/* ── Terminal scrolling ─────────────────────────────────────────────────── */

void arch_terminal_scroll_up(void)                           {}
void arch_terminal_scroll_down(void)                         {}

/* ── Networking ─────────────────────────────────────────────────────────── */

void arch_net_init(void)                                               {}
void arch_net_sock_close_all_for_pid(uint32_t pid)                    { (void)pid; }
int  arch_net_ping(uint32_t ip_be, uint32_t timeout_ms)               { (void)ip_be;(void)timeout_ms; return -1; }
void arch_net_set_config(uint32_t a, uint32_t b, uint32_t c)          { (void)a;(void)b;(void)c; }
void arch_net_get_config(uint32_t *a, uint32_t *b, uint32_t *c)       {
    if (a) *a = 0; if (b) *b = 0; if (c) *c = 0;
}
void arch_net_get_stats(uint32_t *rx, uint32_t *tx)                   {
    if (rx) *rx = 0; if (tx) *tx = 0;
}
int  arch_net_sock_listen(uint16_t port)                               { (void)port; return -1; }
int  arch_net_sock_accept(int fd)                                      { (void)fd; return -1; }
int  arch_net_sock_send(int fd, const void *buf, uint32_t len)         { (void)fd;(void)buf;(void)len; return -1; }
int  arch_net_sock_recv(int fd, void *buf, uint32_t len)               { (void)fd;(void)buf;(void)len; return -1; }
int  arch_net_sock_close(int fd)                                       { (void)fd; return -1; }

/* ── Rust integration ───────────────────────────────────────────────────── */

void arch_rust_test(void) {
    /* Rust library not linked for x86_64. */
}

/* ── QEMU debug exit ────────────────────────────────────────────────────── */

void arch_debug_exit(uint32_t code) {
    /* QEMU_DEBUG_EXIT_PORT = 0xF4, same on both arches. */
    outb(0xF4, (uint8_t)(code & 0xFFu));
}
