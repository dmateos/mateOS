#include "cmd_shared.h"
#include "syscalls.h"

static int sstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sstrncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static int parse_ip4(const char *s, unsigned int *out_be) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    int part = 0;
    unsigned int val = 0;
    for (int i = 0; ; i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            val = val * 10 + (unsigned int)(ch - '0');
            if (val > 255) return -1;
        } else if (ch == '.' || ch == '\0' || ch == ' ') {
            if (part == 0) a = val;
            else if (part == 1) b = val;
            else if (part == 2) c = val;
            else if (part == 3) d = val;
            else return -1;
            part++;
            val = 0;
            if (ch == '\0' || ch == ' ') break;
        } else {
            return -1;
        }
    }
    if (part != 4) return -1;
    *out_be = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

static void print_ip(unsigned int ip_be, const cmd_io_t *io) {
    unsigned int a = (ip_be >> 24) & 0xFF;
    unsigned int b = (ip_be >> 16) & 0xFF;
    unsigned int c = (ip_be >> 8) & 0xFF;
    unsigned int d = ip_be & 0xFF;
    io->print_num((int)a); io->print(".");
    io->print_num((int)b); io->print(".");
    io->print_num((int)c); io->print(".");
    io->print_num((int)d);
}

static void cmd_help(const cmd_io_t *io) {
    io->print("Built-in commands:\n");
    io->print("  help    - Show this help\n");
    io->print("  ls      - List files in ramfs\n");
    io->print("  tasks   - Show running tasks\n");
    io->print("  echo    - Print arguments\n");
    io->print("  ping    - Ping an IP (e.g. ping 10.0.2.2)\n");
    io->print("  ifconfig- Set IP/mask/gw (e.g. ifconfig 10.69.0.69 255.255.255.0 10.69.0.1)\n");
    io->print("  clear   - Clear screen\n");
    io->print("  shutdown- Power off\n");
    io->print("  exit    - ");
    io->print(io->exit_help ? io->exit_help : "Exit");
    io->print("\n");
    io->print("  jobs    - List background jobs\n");
    io->print("\nRun any file by name (e.g. hello.elf)\n");
    io->print("Append '&' to run in background (e.g. httpd.elf &)\n");
}

static void cmd_ls(const cmd_io_t *io) {
    char name[32];
    unsigned int i = 0;
    while (readdir(i, name, sizeof(name)) > 0) {
        io->print("  ");
        io->print(name);
        io->print("\n");
        i++;
    }
    if (i == 0) io->print("  (no files)\n");
}

static void cmd_tasks(const cmd_io_t *io) {
    taskinfo_entry_t tlist[16];
    int count = tasklist(tlist, 16);
    io->print("PID  State    Name\n");
    io->print("---  -------  ----\n");
    for (int i = 0; i < count; i++) {
        io->print_num((int)tlist[i].id);
        io->print("    ");
        switch (tlist[i].state) {
            case 0: io->print("ready  "); break;
            case 1: io->print("run    "); break;
            case 2: io->print("block  "); break;
            default: io->print("???    "); break;
        }
        io->print("  ");
        io->print(tlist[i].name);
        io->print("\n");
    }
}

static void cmd_echo(const char *line, const cmd_io_t *io) {
    if (line[4] == ' ') io->print(line + 5);
    io->print("\n");
}

static void cmd_ping(const char *line, const cmd_io_t *io) {
    const char *arg = line + 4;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        io->print("usage: ping <ip>\n");
        return;
    }
    unsigned int ip_be;
    if (parse_ip4(arg, &ip_be) != 0) {
        io->print("ping: invalid ip\n");
        return;
    }
    if (net_ping(ip_be, 1000) == 0) io->print("ping ok\n");
    else io->print("ping timeout\n");
}

static void cmd_ifconfig(const char *line, const cmd_io_t *io) {
    const char *arg = line + 8;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        unsigned int ip_be = 0, mask_be = 0, gw_be = 0;
        if (net_get(&ip_be, &mask_be, &gw_be) != 0) {
            io->print("ifconfig: failed to read config\n");
            return;
        }
        io->print("ip "); print_ip(ip_be, io); io->print("\n");
        io->print("mask "); print_ip(mask_be, io); io->print("\n");
        io->print("gw "); print_ip(gw_be, io); io->print("\n");
        return;
    }

    unsigned int ip_be = 0, mask_be = 0, gw_be = 0;
    if (parse_ip4(arg, &ip_be) != 0) {
        io->print("ifconfig: invalid ip\n");
        return;
    }
    while (*arg && *arg != ' ') arg++;
    while (*arg == ' ') arg++;
    if (parse_ip4(arg, &mask_be) != 0) {
        io->print("ifconfig: invalid mask\n");
        return;
    }
    while (*arg && *arg != ' ') arg++;
    while (*arg == ' ') arg++;
    if (parse_ip4(arg, &gw_be) != 0) {
        io->print("ifconfig: invalid gw\n");
        return;
    }
    net_cfg(ip_be, mask_be, gw_be);
    io->print("ifconfig ok\n");
}

cmd_result_t cmd_try_builtin(const char *line, const cmd_io_t *io) {
    if (sstrcmp(line, "help") == 0) {
        cmd_help(io);
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "ls") == 0) {
        cmd_ls(io);
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "tasks") == 0) {
        cmd_tasks(io);
        return CMD_HANDLED;
    }
    if (sstrncmp(line, "echo ", 5) == 0 || sstrcmp(line, "echo") == 0) {
        cmd_echo(line, io);
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "clear") == 0) {
        if (io->clear) io->clear();
        return CMD_HANDLED;
    }
    if (sstrncmp(line, "ping ", 5) == 0 || sstrcmp(line, "ping") == 0) {
        cmd_ping(line, io);
        return CMD_HANDLED;
    }
    if (sstrncmp(line, "ifconfig ", 9) == 0 || sstrcmp(line, "ifconfig") == 0) {
        cmd_ifconfig(line, io);
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "shutdown") == 0) {
        io->print("Powering off...\n");
        shutdown();
        return CMD_HANDLED;
    }
    if (sstrcmp(line, "exit") == 0) {
        return CMD_EXIT;
    }
    return CMD_NOT_BUILTIN;
}
