#include "syscalls.h"
#include "libc.h"

#define ROUTE_NONE 0
#define ROUTE_INDEX 1
#define ROUTE_OS 2

static const char *ok_header =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char *not_found_response =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body><h1>404 Not Found</h1><p>Try /index.htm or /os</p></body></html>\n";

static char file_body[8192];
static char os_log[8192];
static char os_page[16384];

static int send_all(int client, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = sock_send(client, buf + sent, (unsigned int)(len - sent));
        if (n > 0) {
            sent += n;
            continue;
        }
        yield();
    }
    return 0;
}

static int has_end_of_headers(const char *buf, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return 1;
        }
    }
    return 0;
}

static int parse_route(const char *req, int req_len) {
    int line_end = 0;
    while (line_end < req_len && req[line_end] != '\r' && req[line_end] != '\n') {
        line_end++;
    }
    if (line_end <= 0) return ROUTE_NONE;
    if (line_end < 6) return ROUTE_NONE;
    if (strncmp(req, "GET ", 4) != 0) return ROUTE_NONE;

    int path_start = 4;
    int path_end = path_start;
    while (path_end < line_end && req[path_end] != ' ') {
        path_end++;
    }
    if (path_end <= path_start) return ROUTE_NONE;

    int path_len = path_end - path_start;
    if (path_len == 1 && req[path_start] == '/') {
        return ROUTE_INDEX;
    }
    if (path_len == 10 && strncmp(req + path_start, "/index.htm", 10) == 0) {
        return ROUTE_INDEX;
    }
    if (path_len == 3 && strncmp(req + path_start, "/os", 3) == 0) {
        return ROUTE_OS;
    }
    if (path_len > 3 && strncmp(req + path_start, "/os?", 4) == 0) {
        return ROUTE_OS;
    }

    return ROUTE_NONE;
}

static int serve_index_htm(int client) {
    int total = 0;
    int fd = open("index.htm", O_RDONLY);
    if (fd < 0) fd = open("/index.htm", O_RDONLY);
    if (fd < 0) return -1;

    while (total < (int)sizeof(file_body)) {
        int n = fread(fd, file_body + total,
                      (unsigned int)(sizeof(file_body) - (unsigned int)total));
        if (n <= 0) break;
        total += n;
    }
    close(fd);

    if (total <= 0) return -1;

    send_all(client, ok_header, strlen(ok_header));
    send_all(client, file_body, total);
    return 0;
}

static int read_file(const char *path, char *dst, int cap) {
    int total = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    while (total < cap) {
        int n = fread(fd, dst + total, (unsigned int)(cap - total));
        if (n <= 0) break;
        total += n;
    }
    close(fd);
    return total;
}

static int append_char(char *dst, int cap, int *len, char c) {
    if (*len >= cap) return -1;
    dst[*len] = c;
    (*len)++;
    return 0;
}

static int append_cstr(char *dst, int cap, int *len, const char *s) {
    while (*s) {
        if (append_char(dst, cap, len, *s++) < 0) return -1;
    }
    return 0;
}

static int append_escaped(char *dst, int cap, int *len, const char *src, int src_len) {
    for (int i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '&') {
            if (append_cstr(dst, cap, len, "&amp;") < 0) return -1;
        } else if (c == '<') {
            if (append_cstr(dst, cap, len, "&lt;") < 0) return -1;
        } else if (c == '>') {
            if (append_cstr(dst, cap, len, "&gt;") < 0) return -1;
        } else if (c == '\"') {
            if (append_cstr(dst, cap, len, "&quot;") < 0) return -1;
        } else {
            if (append_char(dst, cap, len, c) < 0) return -1;
        }
    }
    return 0;
}

static int append_section(int *out_len, const char *title, const char *path) {
    if (append_cstr(os_page, sizeof(os_page), out_len, "<h2>") < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, title) < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "</h2><pre>") < 0) return -1;

    int n = read_file(path, os_log, sizeof(os_log));
    if (n > 0) {
        if (append_escaped(os_page, sizeof(os_page), out_len, os_log, n) < 0) return -1;
    } else {
        if (append_cstr(os_page, sizeof(os_page), out_len, "(unavailable)") < 0) return -1;
    }
    if (append_cstr(os_page, sizeof(os_page), out_len, "</pre>") < 0) return -1;
    return 0;
}

static const char *task_state_name(unsigned int st) {
    if (st == 0) return "ready";
    if (st == 1) return "running";
    if (st == 2) return "blocked";
    if (st == 3) return "terminated";
    return "?";
}

static int append_tasks_section(int *out_len) {
    taskinfo_entry_t t[32];
    int n = tasklist(t, 32);
    if (append_cstr(os_page, sizeof(os_page), out_len, "<h2>tasks</h2><pre>") < 0) return -1;
    if (n <= 0) {
        if (append_cstr(os_page, sizeof(os_page), out_len, "(no tasks)\n</pre>") < 0) return -1;
        return 0;
    }

    if (append_cstr(os_page, sizeof(os_page), out_len, "PID  PPID  RING  STATE       NAME\n") < 0) return -1;
    for (int i = 0; i < n; i++) {
        char num[16];
        itoa((int)t[i].id, num);
        if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, "    ") < 0) return -1;
        itoa((int)t[i].parent_id, num);
        if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, "    ") < 0) return -1;
        itoa((int)t[i].ring, num);
        if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, "    ") < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, task_state_name(t[i].state)) < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, "    ") < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, t[i].name) < 0) return -1;
        if (append_cstr(os_page, sizeof(os_page), out_len, "\n") < 0) return -1;
    }
    if (append_cstr(os_page, sizeof(os_page), out_len, "</pre>") < 0) return -1;
    return 0;
}

static int append_uptime_section(int *out_len) {
    unsigned int ticks = get_ticks();
    unsigned int total = ticks / 100;  // 100Hz timer
    unsigned int d = total / 86400;
    unsigned int h = (total % 86400) / 3600;
    unsigned int m = (total % 3600) / 60;
    unsigned int s = total % 60;
    char num[16];

    if (append_cstr(os_page, sizeof(os_page), out_len, "<h2>uptime</h2><pre>") < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "seconds: ") < 0) return -1;
    itoa((int)total, num); if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "\npretty: ") < 0) return -1;
    itoa((int)d, num); if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "d ") < 0) return -1;
    itoa((int)h, num); if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "h ") < 0) return -1;
    itoa((int)m, num); if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "m ") < 0) return -1;
    itoa((int)s, num); if (append_cstr(os_page, sizeof(os_page), out_len, num) < 0) return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "s\n</pre>") < 0) return -1;
    return 0;
}

static int serve_os_page(int client) {
    int out_len = 0;
    if (append_cstr(os_page, sizeof(os_page), &out_len,
                    "<html><head><title>mateOS /os</title></head><body>") < 0) {
        return -1;
    }
    if (append_cstr(os_page, sizeof(os_page), &out_len,
                    "<h1>mateOS /os</h1><p>virtual kernel files</p>") < 0) {
        return -1;
    }
    if (append_section(&out_len, "cpuinfo.ker", "/cpuinfo.ker") < 0) return -1;
    if (append_section(&out_len, "meminfo.ker", "/meminfo.ker") < 0) return -1;
    if (append_section(&out_len, "lsirq.ker", "/lsirq.ker") < 0) return -1;
    if (append_section(&out_len, "pci.ker", "/pci.ker") < 0) return -1;
    if (append_section(&out_len, "kdebug.ker", "/kdebug.ker") < 0) return -1;
    if (append_tasks_section(&out_len) < 0) return -1;
    if (append_uptime_section(&out_len) < 0) return -1;

    if (append_cstr(os_page, sizeof(os_page), &out_len,
                    "</body></html>\n") < 0) {
        return -1;
    }

    send_all(client, ok_header, strlen(ok_header));
    send_all(client, os_page, out_len);
    return 0;
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int server = sock_listen(80);
    if (server < 0) {
        print("httpd: listen failed\n");
        exit(1);
    }
    print("httpd: listening on port 80\n");

    while (1) {
        int client = sock_accept(server);
        if (client < 0) {
            yield();
            continue;
        }

        char buf[512];
        int total = 0;
        int tries = 0;
        while (total < (int)sizeof(buf) - 1 && tries < 500) {
            int n = sock_recv(client, buf + total, sizeof(buf) - 1 - total);
            if (n > 0) {
                total += n;
                buf[total] = '\0';
                if (has_end_of_headers(buf, total)) break;
            } else if (n == 0) {
                break;
            } else {
                yield();
                tries++;
            }
        }

        if (total > 0) {
            print("httpd: ");
            int end = 0;
            while (end < total && buf[end] != '\r' && buf[end] != '\n') end++;
            write(1, buf, (unsigned int)end);
            print("\n");
        }

        int route = parse_route(buf, total);
        int served = -1;
        if (route == ROUTE_INDEX) {
            served = serve_index_htm(client);
        } else if (route == ROUTE_OS) {
            served = serve_os_page(client);
        }

        if (served < 0) {
            send_all(client, not_found_response, strlen(not_found_response));
        }

        sock_close(client);
    }
}
