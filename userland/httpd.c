#include "syscalls.h"
#include "libc.h"

// Simple substring search
static int has_end_of_headers(const char *buf, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n')
            return 1;
    }
    return 0;
}

static const char *ok_header =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";

static char file_body[8192];

static const char *not_found_response =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body><h1>404 Not Found</h1><p>index.htm not found</p></body></html>\n";

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

static int request_targets_index(const char *req, int req_len) {
    int line_end = 0;
    while (line_end < req_len && req[line_end] != '\r' && req[line_end] != '\n') {
        line_end++;
    }
    if (line_end <= 0) {
        return 0;
    }

    if (line_end >= 6 && strncmp(req, "GET / ", 6) == 0) {
        return 1;
    }
    if (line_end >= 15 && strncmp(req, "GET /index.htm ", 15) == 0) {
        return 1;
    }
    return 0;
}

static int serve_index_htm(int client) {
    int total = 0;
    int fd = open("index.htm", O_RDONLY);
    if (fd < 0) {
        fd = open("/index.htm", O_RDONLY);
    }
    if (fd < 0) {
        return -1;
    }

    while (total < (int)sizeof(file_body)) {
        int n = fread(fd, file_body + total, (unsigned int)(sizeof(file_body) - (unsigned int)total));
        if (n > 0) {
            total += n;
            continue;
        }
        break;
    }
    close(fd);

    if (total <= 0) {
        return -1;
    }

    send_all(client, ok_header, strlen(ok_header));
    send_all(client, file_body, total);
    return 0;
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
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

        // Read HTTP request (drain until end of headers)
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
                break;  // Connection closed
            } else {
                yield();
                tries++;
            }
        }

        // Log the request line (first line of HTTP request)
        if (total > 0) {
            print("httpd: ");
            // Find end of first line
            int end = 0;
            while (end < total && buf[end] != '\r' && buf[end] != '\n') end++;
            write(1, buf, end);
            print("\n");
        }

        // Serve filesystem index.htm or return HTML 404.
        if (total <= 0 || !request_targets_index(buf, total) || serve_index_htm(client) < 0) {
            send_all(client, not_found_response, strlen(not_found_response));
        }

        sock_close(client);
    }
}
