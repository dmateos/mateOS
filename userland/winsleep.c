// Windowed multitasking test: updates once per second.
#include "ugfx.h"
#include "syscalls.h"

#define W 320
#define H 120

static unsigned char buf[W * H];

static void itoa10(int n, char *out) {
    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    char tmp[16];
    int i = 0;
    int neg = 0;
    if (n < 0) {
        neg = 1;
        n = -n;
    }
    while (n > 0 && i < 15) {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    int p = 0;
    if (neg) out[p++] = '-';
    while (i > 0) out[p++] = tmp[--i];
    out[p] = '\0';
}

static void draw_frame(int sec) {
    char num[16];
    itoa10(sec, num);

    ugfx_buf_clear(buf, W, H, 1);
    ugfx_buf_rect(buf, W, H, 0, 0, W, 12, 9);
    ugfx_buf_string(buf, W, H, 6, 2, "Sleep Task", 15);
    ugfx_buf_string(buf, W, H, 8, 24, "This task updates every 1s.", 15);
    ugfx_buf_string(buf, W, H, 8, 40, "Seconds:", 14);
    ugfx_buf_rect(buf, W, H, 70, 40, 64, 10, 1);
    ugfx_buf_string(buf, W, H, 70, 40, num, 15);
    ugfx_buf_string(buf, W, H, 8, 60, "Run multiple instances to test scheduling.", 7);
}

void _start(void) {
    int wid = win_create(W, H, "Sleep");
    if (wid < 0) exit(1);

    int sec = 0;
    while (1) {
        draw_frame(sec);
        win_write(wid, buf, sizeof(buf));
        sleep_ms(1000);
        sec++;
    }
}
