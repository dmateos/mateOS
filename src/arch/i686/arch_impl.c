/*
 * arch_impl.c — i686 implementation of arch_interface.h
 *
 * Every function here is a thin wrapper (or trivial adapter) over the
 * existing i686 code.  No new logic lives here — the real work is still
 * in paging.c, tss.c, gdt.c, interrupts.c, etc.
 *
 * Concrete definitions for the two opaque types:
 *
 *   arch_aspace_t   ≡ page_directory_t   (1024-entry PD)
 *   arch_irq_frame_t ≡ iret_frame_t      (eip/cs/eflags/esp/ss on stack)
 */

#include "arch/arch_interface.h"

/* Pull in all existing i686 headers — implementation details stay here. */
#include "arch/i686/cpu.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/io.h"
#include "arch/i686/legacytty.h"
#include "arch/i686/mouse.h"
#include "arch/i686/paging.h"
#include "arch/i686/pci.h"
#include "arch/i686/tss.h"
#include "arch/i686/util.h"
#include "arch/i686/vga.h"
#include "arch/i686/686init.h"
#include "lib.h"
#include "memlayout.h"
#include "net/net.h"

/* External Rust functions — only available in the i686 build. */
extern void rust_hello(void);
extern int  rust_add(int a, int b);

/* ------------------------------------------------------------------ */
/* Concrete type definitions                                           */
/* ------------------------------------------------------------------ */

/*
 * On i686 the address space IS the page directory.
 * We typedef it as arch_aspace_t via the struct tag.
 */
struct arch_aspace {
    /* Embed the real PD so that cast is a no-op. */
    page_directory_t pd;
};

/*
 * On i686 the interrupt return frame IS the iret_frame_t.
 */
struct arch_irq_frame {
    iret_frame_t f;
};

/* ------------------------------------------------------------------ */
/* Address space                                                       */
/* ------------------------------------------------------------------ */

arch_aspace_t *arch_aspace_create(void) {
    /* paging_create_address_space() returns page_directory_t *,
     * which is layout-compatible with arch_aspace_t *. */
    return (arch_aspace_t *)paging_create_address_space();
}

void arch_aspace_destroy(arch_aspace_t *as) {
    paging_destroy_address_space((page_directory_t *)as);
}

void arch_aspace_switch(arch_aspace_t *as) {
    paging_switch((page_directory_t *)as);
}

arch_aspace_t *arch_aspace_kernel(void) {
    return (arch_aspace_t *)paging_get_kernel_dir();
}

int arch_aspace_map(arch_aspace_t *as, uintptr_t vaddr, uintptr_t paddr,
                    uint32_t flags) {
    /* Translate arch-neutral flags to i686 PAGE_* flags.
     * The values happen to be identical, but going through this
     * translation keeps the boundary explicit. */
    uint32_t hw_flags = 0;
    if (flags & ARCH_PAGE_PRESENT) hw_flags |= PAGE_PRESENT;
    if (flags & ARCH_PAGE_WRITE)   hw_flags |= PAGE_WRITE;
    if (flags & ARCH_PAGE_USER)    hw_flags |= PAGE_USER;
    return paging_map_page((page_directory_t *)as,
                           (uint32_t)vaddr, (uint32_t)paddr, hw_flags);
}

void arch_aspace_unmap(arch_aspace_t *as, uintptr_t vaddr) {
    paging_unmap_page((page_directory_t *)as, (uint32_t)vaddr);
}

void arch_aspace_map_mmio(uintptr_t phys, uint32_t size) {
    paging_map_vbe((uint32_t)phys, size);
}

uintptr_t arch_aspace_user_phys(arch_aspace_t *as, uintptr_t vaddr) {
    page_directory_t *pd = (page_directory_t *)as;
    uint32_t dir_idx   = (uint32_t)vaddr >> 22;
    uint32_t table_idx = ((uint32_t)vaddr >> 12) & 0x3FFu;

    if (!(pd->tables[dir_idx] & PAGE_PRESENT))
        return 0;

    page_table_t *pt = (page_table_t *)PHYS_TO_KVIRT(
        pd->tables[dir_idx] & ~0xFFFu);
    uint32_t pte = pt->pages[table_idx];

    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER))
        return 0;

    return (uintptr_t)(pte & ~0xFFFu);
}

/* ------------------------------------------------------------------ */
/* Task stack initialisation                                           */
/* ------------------------------------------------------------------ */

/*
 * Build the initial kernel-task stack frame that schedule() will pop on
 * the first context switch to this task.  Mirrors the layout pushed by
 * the irq0_task / yield_task stubs in interrupts_asm.S:
 *
 *   [DS][ES][FS][GS]  ← segment registers (popped first)
 *   [EDI..EAX]        ← pusha-equivalent  (8 dwords)
 *   [EIP][CS][EFLAGS] ← iret frame        (kernel privilege, no ESP/SS)
 */
uint32_t *arch_task_init_kernel(void (*entry)(void), uint32_t *stack_top) {
    uint32_t *sp = stack_top;

    /* iret frame (kernel mode — no ring transition, so no ESP/SS) */
    *(--sp) = ARCH_EFLAGS_DEFAULT;   /* EFLAGS */
    *(--sp) = KERNEL_CODE_SEG;       /* CS     */
    *(--sp) = (uint32_t)entry;       /* EIP    */

    /* pusha-equivalent (all zero for a fresh task) */
    *(--sp) = 0; /* EAX */
    *(--sp) = 0; /* ECX */
    *(--sp) = 0; /* EDX */
    *(--sp) = 0; /* EBX */
    *(--sp) = 0; /* ESP (ignored by popa) */
    *(--sp) = 0; /* EBP */
    *(--sp) = 0; /* ESI */
    *(--sp) = 0; /* EDI */

    /* Segment registers */
    *(--sp) = KERNEL_DATA_SEG; /* GS */
    *(--sp) = KERNEL_DATA_SEG; /* FS */
    *(--sp) = KERNEL_DATA_SEG; /* ES */
    *(--sp) = KERNEL_DATA_SEG; /* DS */

    return sp;
}

/*
 * Build the initial kernel stack frame for a user-mode task.
 * The full user-mode iret frame (with ESP and SS for ring-3 → ring-0
 * transition) goes on top, followed by the pusha-equivalent zeros and
 * segment registers, exactly as the irq0_task handler expects.
 */
uint32_t *arch_task_init_user(uintptr_t elf_entry, uintptr_t user_esp,
                              uint32_t *kstack_top) {
    uint32_t *sp = kstack_top;

    /* User-mode iret frame (ring transition includes ESP + SS) */
    *(--sp) = USER_DATA_SEL;         /* SS     */
    *(--sp) = (uint32_t)user_esp;    /* ESP    */
    *(--sp) = ARCH_EFLAGS_DEFAULT;   /* EFLAGS */
    *(--sp) = USER_CODE_SEL;         /* CS     */
    *(--sp) = (uint32_t)elf_entry;   /* EIP    */

    /* pusha-equivalent zeros */
    *(--sp) = 0; /* EAX */
    *(--sp) = 0; /* ECX */
    *(--sp) = 0; /* EDX */
    *(--sp) = 0; /* EBX */
    *(--sp) = 0; /* ESP (ignored) */
    *(--sp) = 0; /* EBP */
    *(--sp) = 0; /* ESI */
    *(--sp) = 0; /* EDI */

    /* Segment registers — user data segment */
    *(--sp) = USER_DATA_SEL; /* GS */
    *(--sp) = USER_DATA_SEL; /* FS */
    *(--sp) = USER_DATA_SEL; /* ES */
    *(--sp) = USER_DATA_SEL; /* DS */

    return sp;
}

void arch_task_set_kernel_stack(uintptr_t kstack_top) {
    tss_set_kernel_stack((uint32_t)kstack_top);
}

/* ------------------------------------------------------------------ */
/* Exec: redirect iret frame                                           */
/* ------------------------------------------------------------------ */

void arch_set_return_context(arch_irq_frame_t *frame, uintptr_t elf_entry,
                             uintptr_t user_esp) {
    /* frame->f is the iret_frame_t sitting on the kernel stack. */
    frame->f.eip    = (uint32_t)elf_entry;
    frame->f.cs     = USER_CODE_SEL;
    frame->f.eflags = ARCH_EFLAGS_DEFAULT;
    frame->f.esp    = (uint32_t)user_esp;
    frame->f.ss     = USER_DATA_SEL;
}

/* ------------------------------------------------------------------ */
/* Syscall glue                                                        */
/* ------------------------------------------------------------------ */

/*
 * The i686 syscall stub (isr128 in interrupts_asm.S) pushes the
 * register set on the kernel stack and then calls:
 *
 *   syscall_handler(eax, ebx, ecx, edx, frame_ptr)
 *
 * We keep that existing ABI for now: state is actually the raw uint32_t
 * passed as the first argument — there is no cpu_state_t * here.
 *
 * The assembly wrapper passes args already unpacked, so
 * arch_syscall_args_from_state / arch_syscall_set_retval are no-ops
 * on i686 (the dispatcher still receives them the old way and calls
 * these only to remain interface-compatible when porting).
 *
 * NOTE: on x86_64 the assembly will call a different entry point that
 * actually uses these functions to unpack the register file.
 */
void arch_syscall_args_from_state(void *state, arch_syscall_args_t *out) {
    /* On i686 the dispatcher receives args directly — this path is
     * only exercised by future arches.  Provide a safe fallback. */
    (void)state;
    (void)out;
}

void arch_syscall_set_retval(void *state, uintptr_t retval) {
    (void)state;
    (void)retval;
}

arch_irq_frame_t *arch_irq_frame_from_state(void *state) {
    /*
     * isr128 passes the iret_frame_t * directly as the 'frame' argument
     * to syscall_handler().  state here is that raw pointer.
     * arch_irq_frame_t and iret_frame_t are layout-compatible.
     */
    return (arch_irq_frame_t *)state;
}

/* ------------------------------------------------------------------ */
/* Architecture initialisation                                         */
/* ------------------------------------------------------------------ */

void arch_init(void) {
    init_686();
}

void arch_paging_init(void) {
    /* i686 paging is fully set up inside init_686() — nothing to do here. */
}

/* ------------------------------------------------------------------ */
/* Halt / shutdown                                                     */
/* ------------------------------------------------------------------ */

void __attribute__((noreturn)) arch_halt_forever(void) {
    /* halt_and_catch_fire() does hlt+ret, so we must loop —
     * interrupts (timer) can wake the CPU from hlt and cause ret to execute. */
    while (1)
        halt_and_catch_fire();
}

/* ------------------------------------------------------------------ */
/* Console output                                                      */
/* ------------------------------------------------------------------ */

void arch_console_putchar(char c) {
    term_putchar(c);
}

/* ------------------------------------------------------------------ */
/* CPU information                                                     */
/* ------------------------------------------------------------------ */

void arch_cpu_get_info(arch_cpu_info_t *out) {
    /* cpu_info_t and arch_cpu_info_t have identical layout. */
    cpu_get_info((cpu_info_t *)out);
}

/* ------------------------------------------------------------------ */
/* PCI bus                                                             */
/* ------------------------------------------------------------------ */

void arch_pci_init(void) {
    pci_init();
}

int arch_pci_get_devices(arch_pci_device_t *out, int max) {
    /* pci_device_t and arch_pci_device_t have identical layout. */
    return pci_get_devices((pci_device_t *)out, max);
}

/* ------------------------------------------------------------------ */
/* Graphics / framebuffer                                              */
/* ------------------------------------------------------------------ */

int arch_gfx_bga_available(void) {
    return vga_bga_available();
}

uint32_t arch_gfx_enter_bga(int width, int height, int bpp) {
    return vga_enter_bga_mode(width, height, bpp);
}

void arch_gfx_exit_bga(void) {
    vga_exit_bga_mode();
}

void arch_gfx_enter_mode13h(void) {
    vga_enter_mode13h();
}

void arch_gfx_enter_text_mode(void) {
    vga_enter_text_mode();
}

uint32_t arch_gfx_mode13h_fb_start(void) {
    return VGA_MODE13H_FB_START;
}

uint32_t arch_gfx_mode13h_fb_end(void) {
    return VGA_MODE13H_FB_END;
}

/* ------------------------------------------------------------------ */
/* PS/2 mouse                                                          */
/* ------------------------------------------------------------------ */

void arch_mouse_init(void) {
    mouse_init();
}

void arch_mouse_irq_handler(uintptr_t irq, uintptr_t vec) {
    /* mouse_irq_handler takes (uint32_t, uint32_t) — safe cast. */
    mouse_irq_handler((uint32_t)irq, (uint32_t)vec);
}

void arch_mouse_get_state(int *x, int *y, uint8_t *buttons) {
    mouse_state_t ms = mouse_get_state();
    if (x)       *x       = ms.x;
    if (y)       *y       = ms.y;
    if (buttons) *buttons = ms.buttons;
}

void arch_mouse_set_bounds(int width, int height) {
    mouse_set_bounds(width, height);
}

/* ------------------------------------------------------------------ */
/* Terminal scrolling                                                  */
/* ------------------------------------------------------------------ */

void arch_terminal_scroll_up(void) {
    terminal_scroll_up();
}

void arch_terminal_scroll_down(void) {
    terminal_scroll_down();
}

/* ------------------------------------------------------------------ */
/* Networking                                                          */
/* ------------------------------------------------------------------ */

void arch_net_init(void) {
    net_init();
}

void arch_net_sock_close_all_for_pid(uint32_t pid) {
    net_sock_close_all_for_pid(pid);
}

int arch_net_ping(uint32_t ip_be, uint32_t timeout_ms) {
    return net_ping(ip_be, timeout_ms);
}

void arch_net_set_config(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be) {
    net_set_config(ip_be, mask_be, gw_be);
}

void arch_net_get_config(uint32_t *ip_be, uint32_t *mask_be, uint32_t *gw_be) {
    net_get_config(ip_be, mask_be, gw_be);
}

void arch_net_get_stats(uint32_t *rx, uint32_t *tx) {
    net_get_stats(rx, tx);
}

int arch_net_sock_listen(uint16_t port) {
    return net_sock_listen(port);
}

int arch_net_sock_accept(int fd) {
    return net_sock_accept(fd);
}

int arch_net_sock_send(int fd, const void *buf, uint32_t len) {
    return net_sock_send(fd, buf, len);
}

int arch_net_sock_recv(int fd, void *buf, uint32_t len) {
    return net_sock_recv(fd, buf, len);
}

int arch_net_sock_close(int fd) {
    return net_sock_close(fd);
}

/* ------------------------------------------------------------------ */
/* Rust integration                                                    */
/* ------------------------------------------------------------------ */

void arch_rust_test(void) {
    rust_hello();
    /* Log Rust add result — callers can ignore the value. */
    int r = rust_add(40, 2);
    kprintf("[boot] Rust test: 40 + 2 = %d\n", r);
}

/* ------------------------------------------------------------------ */
/* QEMU debug exit                                                     */
/* ------------------------------------------------------------------ */

void arch_debug_exit(uint32_t code) {
    outb(QEMU_DEBUG_EXIT_PORT, (uint8_t)(code & 0xFFu));
}
