#include "libc.h"
#include "syscalls.h"

#define ROUTE_NONE 0
#define ROUTE_INDEX 1
#define ROUTE_OS 2

static const char *ok_header = "HTTP/1.0 200 OK\r\n"
                               "Content-Type: text/html\r\n"
                               "Connection: close\r\n"
                               "\r\n";

static const char *not_found_response =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>404 Not Found</title>"
    "<style>body{font-family:monospace;background:#111827;color:#e5e7eb;"
    "padding:24px}"
    "a{color:#93c5fd}</style></head><body><h1>404 Not Found</h1>"
    "<p>Try <a href=\"/\">/</a> or <a "
    "href=\"/index.htm\">/index.htm</a></p></body></html>\n";

static char file_body[8192];
static char os_log[12288];
static char os_page[32768];

static int send_all(int client, const char *buf, int len) {
    int sent = 0;
    int retries = 0;
    while (sent < len) {
        int n = sock_send(client, buf + sent, (unsigned int)(len - sent));
        if (n > 0) {
            sent += n;
            retries = 0;
            continue;
        }
        if (n < 0)
            return -1; // error — stop immediately
        // n == 0: would-block, retry with a limit to avoid infinite loop
        if (++retries > 500)
            return -1;
        yield();
    }
    return 0;
}

static int has_end_of_headers(const char *buf, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            return 1;
        }
    }
    return 0;
}

static int parse_route(const char *req, int req_len) {
    int line_end = 0;
    while (line_end < req_len && req[line_end] != '\r' &&
           req[line_end] != '\n') {
        line_end++;
    }
    if (line_end <= 0)
        return ROUTE_NONE;
    if (line_end < 6)
        return ROUTE_NONE;
    if (strncmp(req, "GET ", 4) != 0)
        return ROUTE_NONE;

    int path_start = 4;
    int path_end = path_start;
    while (path_end < line_end && req[path_end] != ' ') {
        path_end++;
    }
    if (path_end <= path_start)
        return ROUTE_NONE;

    int path_len = path_end - path_start;
    if (path_len == 1 && req[path_start] == '/') {
        return ROUTE_OS;
    }
    if (path_len > 1 && req[path_start] == '/' && req[path_start + 1] == '?') {
        return ROUTE_OS;
    }
    if (path_len == 10 && strncmp(req + path_start, "/index.htm", 10) == 0) {
        return ROUTE_INDEX;
    }
    if (path_len == 3 && strncmp(req + path_start, "/os", 3) == 0) {
        return ROUTE_OS;
    }
    if (path_len == 4 && strncmp(req + path_start, "/os/", 4) == 0) {
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
    if (fd < 0)
        fd = open("/index.htm", O_RDONLY);
    if (fd < 0)
        return -1;

    while (total < (int)sizeof(file_body)) {
        int n =
            fd_read(fd, file_body + total,
                    (unsigned int)(sizeof(file_body) - (unsigned int)total));
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);

    if (total <= 0)
        return -1;

    send_all(client, ok_header, strlen(ok_header));
    send_all(client, file_body, total);
    return 0;
}

static int read_file(const char *path, char *dst, int cap) {
    int total = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    while (total < cap) {
        int n = fd_read(fd, dst + total, (unsigned int)(cap - total));
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);
    return total;
}

static int append_char(char *dst, int cap, int *len, char c) {
    if (*len >= cap)
        return -1;
    dst[*len] = c;
    (*len)++;
    return 0;
}

static int append_cstr(char *dst, int cap, int *len, const char *s) {
    while (*s) {
        if (append_char(dst, cap, len, *s++) < 0)
            return -1;
    }
    return 0;
}

static int append_escaped(char *dst, int cap, int *len, const char *src,
                          int src_len) {
    for (int i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '&') {
            if (append_cstr(dst, cap, len, "&amp;") < 0)
                return -1;
        } else if (c == '<') {
            if (append_cstr(dst, cap, len, "&lt;") < 0)
                return -1;
        } else if (c == '>') {
            if (append_cstr(dst, cap, len, "&gt;") < 0)
                return -1;
        } else if (c == '\"') {
            if (append_cstr(dst, cap, len, "&quot;") < 0)
                return -1;
        } else {
            if (append_char(dst, cap, len, c) < 0)
                return -1;
        }
    }
    return 0;
}

static int append_section(int *out_len, const char *title, const char *path) {
    if (append_cstr(os_page, sizeof(os_page), out_len,
                    "<section class=\"card\"><div class=\"cardhead\"><h2>") < 0)
        return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, title) < 0)
        return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len,
                    "</h2><span class=\"path\">") < 0)
        return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, path) < 0)
        return -1;
    if (append_cstr(os_page, sizeof(os_page), out_len, "</span></div><pre>") <
        0)
        return -1;

    int n = read_file(path, os_log, sizeof(os_log));
    if (n > 0) {
        if (append_escaped(os_page, sizeof(os_page), out_len, os_log, n) < 0)
            return -1;
    } else {
        if (append_cstr(os_page, sizeof(os_page), out_len, "(unavailable)") < 0)
            return -1;
    }
    if (append_cstr(os_page, sizeof(os_page), out_len, "</pre></section>") < 0)
        return -1;
    return 0;
}

static int serve_os_page(int client) {
    int out_len = 0;
    if (append_cstr(
            os_page, sizeof(os_page), &out_len,
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\">"
            "<title>mateOS /os</title>"
            "<style>"
            "body{margin:0;font-family:monospace;background:#0b1220;color:#"
            "dbe4f0}"
            ".wrap{max-width:1100px;margin:0 auto;padding:20px}"
            ".hero{background:#111a2d;border:1px solid "
            "#263247;border-radius:12px;padding:16px 18px;"
            "box-shadow:0 8px 24px rgba(0,0,0,.25)}"
            ".hero h1{margin:0 0 6px 0;font-size:22px;color:#f8fafc}"
            ".muted{color:#9fb0c6;margin:0}"
            ".links{margin-top:10px}"
            ".links a{display:inline-block;margin-right:8px;padding:4px "
            "8px;border-radius:7px;"
            "background:#1a2740;border:1px solid "
            "#314566;color:#c7dcff;text-decoration:none}"
            ".grid{display:grid;grid-template-columns:1fr;gap:12px;margin-top:"
            "14px}"
            ".card{background:#111a2d;border:1px solid "
            "#263247;border-radius:12px;overflow:hidden}"
            ".cardhead{display:flex;justify-content:space-between;align-items:"
            "center;"
            "padding:10px 12px;background:#0f1728;border-bottom:1px solid "
            "#263247;gap:10px}"
            ".card h2{margin:0;font-size:14px;color:#e6eefb}"
            ".path{font-size:11px;color:#8fa3be}"
            "pre{margin:0;padding:12px;white-space:pre-wrap;word-break:break-"
            "word;"
            "color:#dbe4f0;background:#111a2d;max-height:280px;overflow:auto}"
            "@media(min-width:900px){.grid{grid-template-columns:1fr 1fr}}"
            "</style></head><body><div class=\"wrap\">") < 0) {
        return -1;
    }
    if (append_cstr(
            os_page, sizeof(os_page), &out_len,
            "<div class=\"hero\"><h1>mateOS system status</h1>"
            "<p class=\"muted\">Virtual kernel files exposed over httpd</p>"
            "<div class=\"links\"><a href=\"/\">/</a><a "
            "href=\"/index.htm\">index.htm</a>"
            "<a href=\"/os\">legacy /os</a></div></div><div class=\"grid\">") <
        0) {
        return -1;
    }
    if (append_section(&out_len, "kcpu", "/mos/kcpu") < 0)
        return -1;
    if (append_section(&out_len, "kmem", "/mos/kmem") < 0)
        return -1;
    if (append_section(&out_len, "kirq", "/mos/kirq") < 0)
        return -1;
    if (append_section(&out_len, "kpci", "/mos/kpci") < 0)
        return -1;
    if (append_section(&out_len, "kuptime", "/mos/kuptime") < 0)
        return -1;
    if (append_section(&out_len, "kwin", "/mos/kwin") < 0)
        return -1;
    if (append_section(&out_len, "kvfs", "/mos/kvfs") < 0)
        return -1;
    if (append_section(&out_len, "kheap", "/mos/kheap") < 0)
        return -1;
    if (append_section(&out_len, "knet", "/mos/knet") < 0)
        return -1;
    if (append_section(&out_len, "ktasks", "/mos/ktasks") < 0)
        return -1;
    if (append_section(&out_len, "kdebug", "/mos/kdebug") < 0)
        return -1;
    if (append_section(&out_len, "kver", "/mos/kver") < 0)
        return -1;

    if (append_cstr(os_page, sizeof(os_page), &out_len,
                    "</div></div></body></html>\n") < 0) {
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
                if (has_end_of_headers(buf, total))
                    break;
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
            while (end < total && buf[end] != '\r' && buf[end] != '\n')
                end++;
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
