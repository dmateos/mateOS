#include "kernel.h"
#include "lib.h"

#include "arch/arch.h"
#include "boot/multiboot.h"
#include "fs/fat16.h"
#include "fs/vfs.h"
#include "io/console.h"
#include "io/keyboard.h"
#include "io/window.h"
#include "liballoc/liballoc_1_1.h"
#include "memlayout.h"
#include "net/net.h"
#include "proc/pmm.h"
#include "proc/task.h"
#include "syscall.h"
#include "version.h"

// External Rust functions
extern void rust_hello(void);
extern int rust_add(int a, int b);

static int cmdline_has_token(const char *cmdline, const char *token) {
    if (!cmdline || !token || !token[0])
        return 0;
    size_t tlen = strlen(token);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != ' ')
            p++;
        size_t len = (size_t)(p - start);
        if (len == tlen && memcmp(start, token, tlen) == 0)
            return 1;
    }
    return 0;
}

static int cmdline_get_value(const char *cmdline, const char *key, char *out,
                             size_t out_cap) {
    if (!cmdline || !key || !key[0] || !out || out_cap < 2)
        return 0;
    size_t klen = strlen(key);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != ' ')
            p++;
        size_t len = (size_t)(p - start);
        if (len > klen + 1 && start[klen] == '=' &&
            memcmp(start, key, klen) == 0) {
            size_t vlen = len - (klen + 1);
            if (vlen >= out_cap)
                vlen = out_cap - 1;
            memcpy(out, start + klen + 1, vlen);
            out[vlen] = 0;
            return 1;
        }
    }
    return 0;
}

static int str_ends_with(const char *s, const char *sfx) {
    if (!s || !sfx)
        return 0;
    size_t ls = strlen(s);
    size_t lx = strlen(sfx);
    if (lx > ls)
        return 0;
    return memcmp(s + (ls - lx), sfx, lx) == 0;
}

void kernel_main(uint32_t multiboot_magic, multiboot_info_t *multiboot_info) {
    init_686();
    kprintf("[boot] mateOS %s (abi=%d, built=%s)\n", KERNEL_VERSION_FULL,
            KERNEL_VERSION_ABI, KERNEL_BUILD_DATE_UTC);
    kprintf("[boot] paging init ok\n");

    // Parse multiboot info (if provided by bootloader)
    // The bootloader passes a physical pointer; convert to higher-half VA.
    multiboot_info = (multiboot_info_t *)PHYS_TO_KVIRT((uint32_t)multiboot_info);
    printf("\n");
    multiboot_init(multiboot_magic, multiboot_info);
    const char *cmdline = multiboot_get_cmdline();
    if (cmdline_has_token(cmdline, "serial=1") ||
        cmdline_get_value(cmdline, "autorun", (char[2]){0}, 2)) {
        serial_init();
        console_set_serial_mirror(1);
        kprintf("[boot] serial mirror enabled\n");
    }

    // Detect RAM size from multiboot memory map (cap at 1GB, higher-half limit)
    uint32_t ram_top = multiboot_detect_ram_top(PMM_MAX_END);
    if (!ram_top)
        ram_top = 0x2000000u; // Fallback: 32MB

    // Initialize physical memory manager
    pmm_init(ram_top);
    kprintf("[boot] pmm init ok — %d MB RAM, %d frames (0x%x-0x%x)\n",
            ram_top / (1024 * 1024), PMM_FRAME_COUNT, PMM_START, PMM_END);

    printf("\n");

    keyboard_init_interrupts();

    // Test Rust integration on boot
    printf("\n");
    rust_hello();
    printf("Rust test: 40 + 2 = %d\n\n", rust_add(40, 2));

    // Scan PCI bus
    pci_init();
    kprintf("[boot] pci scan ok\n");

    // Initialize network (RTL8139 + minimal ARP/ICMP)
    net_init();
    kprintf("[boot] net init ok\n");

    // Initialize VFS and register FAT16 boot filesystem
    vfs_init();
    kprintf("[boot] vfs init ok\n");
    if (fat16_init() != 0) {
        printf("FATAL: FAT16 boot disk not found. Cannot boot.\n");
        printf("Ensure an IDE disk with FAT16 filesystem is attached.\n");
        while (1)
            halt_and_catch_fire();
    }
    vfs_register_fs(fat16_get_ops());
    kprintf("[boot] fat16 boot disk ok\n");

    // Initialize task system
    task_init();
    kprintf("[boot] task init ok\n");

    // Initialize syscall handler
    syscall_init();
    kprintf("[boot] syscall init ok\n");

    // Initialize window manager subsystem
    window_init();
    kprintf("[boot] window init ok\n");

    // Initialize PS/2 mouse
    mouse_init();
    register_interrupt_handler(0x2C, mouse_irq_handler);
    pic_unmask_irq(12);

    // Print boot summary with RAM and PMM stats
    {
        uint32_t pmm_total, pmm_used, pmm_free;
        pmm_get_stats(&pmm_total, &pmm_used, &pmm_free);
        kprintf("[boot] RAM: %d MB | PMM: %d/%d frames free (%d MB avail)\n",
                ram_top / (1024 * 1024), pmm_free, pmm_total,
                (pmm_free * 0x1000) / (1024 * 1024));
        kprintf("[boot] user VA: 0x%x-0x%x (~%d MB)\n", USER_REGION_START,
                USER_REGION_END,
                (USER_REGION_END - USER_REGION_START) / (1024 * 1024));
    }

    // Boot message
    console_init();

    // Enable keyboard buffer for userland shell
    keyboard_buffer_init();
    keyboard_buffer_enable(1);

    // Executables live in /bin/ on the FAT16 boot disk
    const char *boot_prog = "bin/init.elf";
    char autorun_name[64];
    char autorun_prog[72];
    if (cmdline_get_value(cmdline, "autorun", autorun_name,
                          sizeof(autorun_name))) {
        memset(autorun_prog, 0, sizeof(autorun_prog));
        // Prepend bin/ prefix for autorun programs
        memcpy(autorun_prog, "bin/", 4);
        size_t n = strlen(autorun_name);
        if (n >= sizeof(autorun_prog) - 5)
            n = sizeof(autorun_prog) - 5;
        memcpy(autorun_prog + 4, autorun_name, n);
        autorun_prog[4 + n] = 0;
        if (!str_ends_with(autorun_prog, ".elf") &&
            4 + n + 4 < sizeof(autorun_prog)) {
            memcpy(autorun_prog + 4 + n, ".elf", 5);
        }
        boot_prog = autorun_prog;
        kprintf("[boot] autorun requested: %s\n", boot_prog);
    }

    // Auto-launch boot program from FAT16 boot disk
    task_t *boot_task = task_create_user_elf(boot_prog, NULL, 0);
    if (boot_task) {
        task_enable();
    } else {
        printf("WARNING: %s not found on boot disk\n", boot_prog);
        printf("No boot program available. System halted.\n");
    }

    // Main loop - just halt and wait for interrupts
    while (1) {
        halt_and_catch_fire();
    }
}
