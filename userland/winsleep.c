// Windowed multitasking test: updates once per second.
#include "libc.h"
#include "syscalls.h"
#include "ugfx.h"

#define W 320
#define H 120

static unsigned char buf[W * H];

static void draw_frame(int sec) {
    char num[16];
    itoa(sec, num);

    ugfx_buf_clear(buf, W, H, 1);
    ugfx_buf_rect(buf, W, H, 0, 0, W, 12, 9);
    ugfx_buf_string(buf, W, H, 6, 2, "Sleep Task", 15);
    ugfx_buf_string(buf, W, H, 8, 24, "This task updates every 1s.", 15);
    ugfx_buf_string(buf, W, H, 8, 40, "Seconds:", 14);
    ugfx_buf_rect(buf, W, H, 70, 40, 64, 10, 1);
    ugfx_buf_string(buf, W, H, 70, 40, num, 15);
    ugfx_buf_string(buf, W, H, 8, 60,
                    "Run multiple instances to test scheduling.", 7);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int wid = win_create(W, H, "Sleep");
    if (wid < 0) {
        print("error: requires window manager\n");
        exit(1);
    }
    detach();

    int sec = 0;
    while (1) {
        int k = win_getkey(wid);
        if (k == 27 || k == 'q' || k == 'Q')
            break;

        draw_frame(sec);
        win_write(wid, buf, sizeof(buf));

        for (int ms = 0; ms < 1000; ms += 50) {
            k = win_getkey(wid);
            if (k == 27 || k == 'q' || k == 'Q') {
                win_destroy(wid);
                exit(0);
            }
            sleep_ms(50);
        }
        sec++;
    }

    win_destroy(wid);
    exit(0);
}
