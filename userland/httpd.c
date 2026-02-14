#include "syscalls.h"

static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

static int strlen_s(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

// Simple substring search
static int has_end_of_headers(const char *buf, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n')
            return 1;
    }
    return 0;
}

static const char *response =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body>"
    "<h1>Hello from mateOS!</h1>"
    "<p>This page is served by a userland HTTP server "
    "running on a custom x86 operating system.</p>"
    "</body></html>";

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

        // Send HTTP response
        int rlen = strlen_s(response);
        int sent = 0;
        while (sent < rlen) {
            int n = sock_send(client, response + sent, rlen - sent);
            if (n > 0) {
                sent += n;
            } else {
                yield();
            }
        }

        sock_close(client);
    }
}
