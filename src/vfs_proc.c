#include "vfs_proc.h"

#include "vfs.h"
#include "liballoc/liballoc_hooks.h"
#include "pmm.h"
#include "arch/i686/util.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/pci.h"
#include "arch/i686/timer.h"
#include "window.h"
#include "task.h"
#include "net.h"
#include "version.h"

static char vgen_buf[4096];

typedef uint32_t (*vgen_fn_t)(char *dst, uint32_t cap);

static int append_char(char *dst, uint32_t cap, uint32_t *len, char c) {
    if (*len >= cap) return -1;
    dst[*len] = c;
    (*len)++;
    return 0;
}

static int append_cstr(char *dst, uint32_t cap, uint32_t *len, const char *s) {
    while (*s) {
        if (append_char(dst, cap, len, *s++) < 0) return -1;
    }
    return 0;
}

static int append_dec_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v) {
    if (v == 0) return append_char(dst, cap, len, '0');
    char tmp[16];
    int t = 0;
    while (v > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = t - 1; i >= 0; i--) {
        if (append_char(dst, cap, len, tmp[i]) < 0) return -1;
    }
    return 0;
}

static int append_hex_u32(char *dst, uint32_t cap, uint32_t *len, uint32_t v) {
    if (append_cstr(dst, cap, len, "0x") < 0) return -1;
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nib = (v >> (uint32_t)shift) & 0xF;
        if (!started && nib == 0 && shift > 0) continue;
        started = 1;
        char c = (char)(nib < 10 ? ('0' + nib) : ('a' + (nib - 10)));
        if (append_char(dst, cap, len, c) < 0) return -1;
    }
    if (!started) {
        if (append_char(dst, cap, len, '0') < 0) return -1;
    }
    return 0;
}

static uint32_t vgen_meminfo(char *dst, uint32_t cap) {
    uint32_t len = 0;
    uint32_t total = 0, used = 0, free_frames = 0;
    uint32_t hstart = 0, hend = 0, hcur = 0;

    pmm_get_stats(&total, &used, &free_frames);
    liballoc_heap_info(&hstart, &hend, &hcur);

    uint32_t htotal = hend - hstart;
    uint32_t hused = (hcur > hstart) ? (hcur - hstart) : 0;
    uint32_t hfree = (htotal > hused) ? (htotal - hused) : 0;

    append_cstr(dst, cap, &len, "PMM: total=");
    append_dec_u32(dst, cap, &len, total);
    append_cstr(dst, cap, &len, " used=");
    append_dec_u32(dst, cap, &len, used);
    append_cstr(dst, cap, &len, " free=");
    append_dec_u32(dst, cap, &len, free_frames);
    append_cstr(dst, cap, &len, " frames (4KB each)\n");

    append_cstr(dst, cap, &len, "Heap: start=");
    append_hex_u32(dst, cap, &len, hstart);
    append_cstr(dst, cap, &len, " end=");
    append_hex_u32(dst, cap, &len, hend);
    append_cstr(dst, cap, &len, " cur=");
    append_hex_u32(dst, cap, &len, hcur);
    append_cstr(dst, cap, &len, "\nHeap: used=");
    append_dec_u32(dst, cap, &len, hused);
    append_cstr(dst, cap, &len, " bytes free=");
    append_dec_u32(dst, cap, &len, hfree);
    append_cstr(dst, cap, &len, " bytes total=");
    append_dec_u32(dst, cap, &len, htotal);
    append_cstr(dst, cap, &len, " bytes\n");

    return len;
}

static uint32_t vgen_cpuinfo(char *dst, uint32_t cap) {
    uint32_t len = 0;
    cpu_info_t info;
    cpu_get_info(&info);

    append_cstr(dst, cap, &len, "CPU vendor: ");
    append_cstr(dst, cap, &len, info.vendor);
    append_cstr(dst, cap, &len, "\nCPUID max leaf: ");
    append_hex_u32(dst, cap, &len, info.max_leaf);
    append_cstr(dst, cap, &len, "\nFamily: ");
    append_dec_u32(dst, cap, &len, info.family);
    append_cstr(dst, cap, &len, "  Model: ");
    append_dec_u32(dst, cap, &len, info.model);
    append_cstr(dst, cap, &len, "  Stepping: ");
    append_dec_u32(dst, cap, &len, info.stepping);
    append_cstr(dst, cap, &len, "\nFeature ECX: ");
    append_hex_u32(dst, cap, &len, info.feature_ecx);
    append_cstr(dst, cap, &len, "\nFeature EDX: ");
    append_hex_u32(dst, cap, &len, info.feature_edx);
    append_cstr(dst, cap, &len, "\n");

    return len;
}

static uint32_t vgen_lsirq(char *dst, uint32_t cap) {
    uint32_t len = 0;
    irq_info_t irq[16];
    int count = irq_get_snapshot(irq, 16);

    append_cstr(dst, cap, &len, "IRQ  Vec  Masked  Handler  Addr        Name\n");
    for (int i = 0; i < count; i++) {
        append_dec_u32(dst, cap, &len, irq[i].irq);
        append_cstr(dst, cap, &len, "    ");
        append_hex_u32(dst, cap, &len, irq[i].vec);
        append_cstr(dst, cap, &len, "   ");
        append_cstr(dst, cap, &len, irq[i].masked ? "yes" : "no");
        append_cstr(dst, cap, &len, "      ");
        append_cstr(dst, cap, &len, irq[i].has_handler ? "yes" : "no");
        append_cstr(dst, cap, &len, "      ");
        if (irq[i].handler_addr) {
            append_hex_u32(dst, cap, &len, irq[i].handler_addr);
        } else {
            append_cstr(dst, cap, &len, "-");
        }
        append_cstr(dst, cap, &len, "    ");
        if (irq[i].handler_name && irq[i].handler_name[0]) {
            append_cstr(dst, cap, &len, irq[i].handler_name);
        } else {
            append_cstr(dst, cap, &len, "-");
        }
        append_cstr(dst, cap, &len, "\n");
    }

    return len;
}

static uint32_t vgen_pci(char *dst, uint32_t cap) {
    uint32_t len = 0;
    pci_device_t devs[PCI_MAX_DEVICES];
    int count = pci_get_devices(devs, PCI_MAX_DEVICES);

    append_cstr(dst, cap, &len, "PCI devices (");
    append_dec_u32(dst, cap, &len, (uint32_t)count);
    append_cstr(dst, cap, &len, "):\n");

    for (int i = 0; i < count; i++) {
        pci_device_t *d = &devs[i];
        append_cstr(dst, cap, &len, "  ");
        append_dec_u32(dst, cap, &len, d->bus);
        append_cstr(dst, cap, &len, ":");
        append_dec_u32(dst, cap, &len, d->device);
        append_cstr(dst, cap, &len, ".");
        append_dec_u32(dst, cap, &len, d->function);
        append_cstr(dst, cap, &len, " vendor=");
        append_hex_u32(dst, cap, &len, d->vendor_id);
        append_cstr(dst, cap, &len, " device=");
        append_hex_u32(dst, cap, &len, d->device_id);
        append_cstr(dst, cap, &len, " class=");
        append_hex_u32(dst, cap, &len, d->class_code);
        append_cstr(dst, cap, &len, ".");
        append_hex_u32(dst, cap, &len, d->subclass);
        if (d->irq_line && d->irq_line != 0xFF) {
            append_cstr(dst, cap, &len, " irq=");
            append_dec_u32(dst, cap, &len, d->irq_line);
        }
        append_cstr(dst, cap, &len, "\n");
    }

    return len;
}

static uint32_t vgen_uptime(char *dst, uint32_t cap) {
    uint32_t len = 0;
    uint32_t ticks = get_tick_count();
    uint32_t total = get_uptime_seconds();
    uint32_t d = total / 86400;
    uint32_t h = (total % 86400) / 3600;
    uint32_t m = (total % 3600) / 60;
    uint32_t s = total % 60;

    append_cstr(dst, cap, &len, "ticks: ");
    append_dec_u32(dst, cap, &len, ticks);
    append_cstr(dst, cap, &len, "\nseconds: ");
    append_dec_u32(dst, cap, &len, total);
    append_cstr(dst, cap, &len, "\npretty: ");
    append_dec_u32(dst, cap, &len, d);
    append_cstr(dst, cap, &len, "d ");
    append_dec_u32(dst, cap, &len, h);
    append_cstr(dst, cap, &len, "h ");
    append_dec_u32(dst, cap, &len, m);
    append_cstr(dst, cap, &len, "m ");
    append_dec_u32(dst, cap, &len, s);
    append_cstr(dst, cap, &len, "s\n");
    return len;
}

static uint32_t vgen_windows(char *dst, uint32_t cap) {
    uint32_t len = 0;
    win_info_t info[MAX_WINDOWS];
    int count = window_list(info, MAX_WINDOWS);

    append_cstr(dst, cap, &len, "windows: ");
    append_dec_u32(dst, cap, &len, (uint32_t)count);
    append_cstr(dst, cap, &len, "\nID   PID   W    H    TITLE\n");
    for (int i = 0; i < count; i++) {
        append_dec_u32(dst, cap, &len, (uint32_t)info[i].window_id);
        append_cstr(dst, cap, &len, "   ");
        append_dec_u32(dst, cap, &len, info[i].owner_pid);
        append_cstr(dst, cap, &len, "   ");
        append_dec_u32(dst, cap, &len, (uint32_t)info[i].w);
        append_cstr(dst, cap, &len, "   ");
        append_dec_u32(dst, cap, &len, (uint32_t)info[i].h);
        append_cstr(dst, cap, &len, "   ");
        append_cstr(dst, cap, &len, info[i].title);
        append_cstr(dst, cap, &len, "\n");
    }
    return len;
}

static uint32_t vgen_vfs(char *dst, uint32_t cap) {
    uint32_t len = 0;
    int fs_count = vfs_get_registered_fs_count();
    int vf_count = vfs_get_virtual_file_count();
    append_cstr(dst, cap, &len, "filesystems: ");
    append_dec_u32(dst, cap, &len, (uint32_t)fs_count);
    append_cstr(dst, cap, &len, "\n");
    for (int i = 0; i < fs_count; i++) {
        append_cstr(dst, cap, &len, "  fs");
        append_dec_u32(dst, cap, &len, (uint32_t)i);
        append_cstr(dst, cap, &len, ": ");
        append_cstr(dst, cap, &len, vfs_get_registered_fs_name(i));
        append_cstr(dst, cap, &len, "\n");
    }

    append_cstr(dst, cap, &len, "virtual files: ");
    append_dec_u32(dst, cap, &len, (uint32_t)vf_count);
    append_cstr(dst, cap, &len, "\n");
    for (int i = 0; i < vf_count; i++) {
        append_cstr(dst, cap, &len, "  /");
        append_cstr(dst, cap, &len, vfs_get_virtual_file_name(i));
        append_cstr(dst, cap, &len, "\n");
    }
    return len;
}

static uint32_t vgen_heap(char *dst, uint32_t cap) {
    uint32_t len = 0;
    uint32_t hstart = 0, hend = 0, hcur = 0;
    liballoc_heap_info(&hstart, &hend, &hcur);
    uint32_t htotal = hend - hstart;
    uint32_t hused = (hcur > hstart) ? (hcur - hstart) : 0;
    uint32_t hfree = (htotal > hused) ? (htotal - hused) : 0;

    append_cstr(dst, cap, &len, "heap.start: ");
    append_hex_u32(dst, cap, &len, hstart);
    append_cstr(dst, cap, &len, "\nheap.end: ");
    append_hex_u32(dst, cap, &len, hend);
    append_cstr(dst, cap, &len, "\nheap.cur: ");
    append_hex_u32(dst, cap, &len, hcur);
    append_cstr(dst, cap, &len, "\nheap.used_bytes: ");
    append_dec_u32(dst, cap, &len, hused);
    append_cstr(dst, cap, &len, "\nheap.free_bytes: ");
    append_dec_u32(dst, cap, &len, hfree);
    append_cstr(dst, cap, &len, "\nheap.total_bytes: ");
    append_dec_u32(dst, cap, &len, htotal);
    append_cstr(dst, cap, &len, "\n");
    return len;
}

static void append_ip_be(char *dst, uint32_t cap, uint32_t *len, uint32_t ip_be) {
    append_dec_u32(dst, cap, len, (ip_be >> 24) & 0xFF);
    append_cstr(dst, cap, len, ".");
    append_dec_u32(dst, cap, len, (ip_be >> 16) & 0xFF);
    append_cstr(dst, cap, len, ".");
    append_dec_u32(dst, cap, len, (ip_be >> 8) & 0xFF);
    append_cstr(dst, cap, len, ".");
    append_dec_u32(dst, cap, len, ip_be & 0xFF);
}

static const char *task_state_name(uint32_t st) {
    if (st == 0) return "ready";
    if (st == 1) return "running";
    if (st == 2) return "blocked";
    if (st == 3) return "terminated";
    return "?";
}

static uint32_t vgen_tasks(char *dst, uint32_t cap) {
    uint32_t len = 0;
    taskinfo_entry_t t[32];
    int n = task_list_info(t, 32);
    if (n < 0) n = 0;

    append_cstr(dst, cap, &len, "PID  PPID  RING  STATE       NAME\n");
    for (int i = 0; i < n; i++) {
        append_dec_u32(dst, cap, &len, t[i].id);
        append_cstr(dst, cap, &len, "    ");
        append_dec_u32(dst, cap, &len, t[i].parent_id);
        append_cstr(dst, cap, &len, "    ");
        append_dec_u32(dst, cap, &len, t[i].ring);
        append_cstr(dst, cap, &len, "    ");
        append_cstr(dst, cap, &len, task_state_name(t[i].state));
        append_cstr(dst, cap, &len, "    ");
        append_cstr(dst, cap, &len, t[i].name);
        append_cstr(dst, cap, &len, "\n");
    }
    return len;
}

static uint32_t vgen_net(char *dst, uint32_t cap) {
    uint32_t len = 0;
    uint32_t ip_be = 0, mask_be = 0, gw_be = 0;
    uint32_t rx = 0, tx = 0;
    net_get_config(&ip_be, &mask_be, &gw_be);
    net_get_stats(&rx, &tx);

    append_cstr(dst, cap, &len, "ip   ");
    append_ip_be(dst, cap, &len, ip_be);
    append_cstr(dst, cap, &len, "\nmask ");
    append_ip_be(dst, cap, &len, mask_be);
    append_cstr(dst, cap, &len, "\ngw   ");
    append_ip_be(dst, cap, &len, gw_be);
    append_cstr(dst, cap, &len, "\nrxpk ");
    append_dec_u32(dst, cap, &len, rx);
    append_cstr(dst, cap, &len, "\ntxpk ");
    append_dec_u32(dst, cap, &len, tx);
    append_cstr(dst, cap, &len, "\n");
    return len;
}

static uint32_t vgen_version(char *dst, uint32_t cap) {
    uint32_t len = 0;
    append_cstr(dst, cap, &len, "version: ");
    append_cstr(dst, cap, &len, KERNEL_VERSION_STR);
    append_cstr(dst, cap, &len, "\ngit: ");
    append_cstr(dst, cap, &len, KERNEL_VERSION_GIT);
    append_cstr(dst, cap, &len, "\nabi: ");
    append_dec_u32(dst, cap, &len, KERNEL_VERSION_ABI);
    append_cstr(dst, cap, &len, "\nbuilt_utc: ");
    append_cstr(dst, cap, &len, KERNEL_BUILD_DATE_UTC);
    append_cstr(dst, cap, &len, "\nfull: ");
    append_cstr(dst, cap, &len, KERNEL_VERSION_FULL);
    append_cstr(dst, cap, &len, "\n");
    return len;
}

static int vfile_read_from_generated(vgen_fn_t gen, uint32_t offset, void *buf, uint32_t len) {
    if (!buf || len == 0) return 0;
    uint32_t total = gen(vgen_buf, sizeof(vgen_buf));
    if (offset >= total) return 0;
    uint32_t remaining = total - offset;
    if (len > remaining) len = remaining;
    memcpy(buf, vgen_buf + offset, len);
    return (int)len;
}

static uint32_t vfile_size_from_generated(vgen_fn_t gen) {
    return gen(vgen_buf, sizeof(vgen_buf));
}

static uint32_t vfile_kdebug_size(void) { return klog_snapshot_size(); }
static int vfile_kdebug_read(uint32_t offset, void *buf, uint32_t len) { return klog_read_bytes(offset, buf, len); }
static uint32_t vfile_meminfo_size(void) { return vfile_size_from_generated(vgen_meminfo); }
static int vfile_meminfo_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_meminfo, offset, buf, len); }
static uint32_t vfile_cpuinfo_size(void) { return vfile_size_from_generated(vgen_cpuinfo); }
static int vfile_cpuinfo_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_cpuinfo, offset, buf, len); }
static uint32_t vfile_lsirq_size(void) { return vfile_size_from_generated(vgen_lsirq); }
static int vfile_lsirq_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_lsirq, offset, buf, len); }
static uint32_t vfile_pci_size(void) { return vfile_size_from_generated(vgen_pci); }
static int vfile_pci_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_pci, offset, buf, len); }
static uint32_t vfile_uptime_size(void) { return vfile_size_from_generated(vgen_uptime); }
static int vfile_uptime_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_uptime, offset, buf, len); }
static uint32_t vfile_windows_size(void) { return vfile_size_from_generated(vgen_windows); }
static int vfile_windows_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_windows, offset, buf, len); }
static uint32_t vfile_vfs_size(void) { return vfile_size_from_generated(vgen_vfs); }
static int vfile_vfs_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_vfs, offset, buf, len); }
static uint32_t vfile_heap_size(void) { return vfile_size_from_generated(vgen_heap); }
static int vfile_heap_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_heap, offset, buf, len); }
static uint32_t vfile_tasks_size(void) { return vfile_size_from_generated(vgen_tasks); }
static int vfile_tasks_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_tasks, offset, buf, len); }
static uint32_t vfile_net_size(void) { return vfile_size_from_generated(vgen_net); }
static int vfile_net_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_net, offset, buf, len); }
static uint32_t vfile_version_size(void) { return vfile_size_from_generated(vgen_version); }
static int vfile_version_read(uint32_t offset, void *buf, uint32_t len) { return vfile_read_from_generated(vgen_version, offset, buf, len); }

void vfs_proc_register_files(void) {
    vfs_register_virtual_file("kdebug.mos", vfile_kdebug_size, vfile_kdebug_read);
    vfs_register_virtual_file("kmeminfo.mos", vfile_meminfo_size, vfile_meminfo_read);
    vfs_register_virtual_file("kcpuinfo.mos", vfile_cpuinfo_size, vfile_cpuinfo_read);
    vfs_register_virtual_file("kirq.mos", vfile_lsirq_size, vfile_lsirq_read);
    vfs_register_virtual_file("kpci.mos", vfile_pci_size, vfile_pci_read);
    vfs_register_virtual_file("kuptime.mos", vfile_uptime_size, vfile_uptime_read);
    vfs_register_virtual_file("kwin.mos", vfile_windows_size, vfile_windows_read);
    vfs_register_virtual_file("kvfs.mos", vfile_vfs_size, vfile_vfs_read);
    vfs_register_virtual_file("kheap.mos", vfile_heap_size, vfile_heap_read);
    vfs_register_virtual_file("ktasks.mos", vfile_tasks_size, vfile_tasks_read);
    vfs_register_virtual_file("knet.mos", vfile_net_size, vfile_net_read);
    vfs_register_virtual_file("kversion.mos", vfile_version_size, vfile_version_read);
}
