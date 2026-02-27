#include "libc.h"
#include "syscalls.h"
#include "ugfx.h"

#define W 500
#define H 350

static unsigned char buf[W * H];

static const char *oracle_lines[] = {
    "IN THE BEGINNING WAS THE TASK", "BLESSED ARE THE LOW LATENCIES",
    "SEEK AND YE SHALL OPEN",        "THE KERNEL SAW IT WAS GOOD",
    "INTERRUPTS SHALL AWAKEN THEE",  "ALL GLORY TO THE SCHEDULER",
    "HEAP IS VANITY; STACK IS DUST"};

static unsigned int rng_state = 0xC0FFEEu;

static unsigned int rng_next(void) {
    rng_state ^= (rng_state << 13);
    rng_state ^= (rng_state >> 17);
    rng_state ^= (rng_state << 5);
    return rng_state;
}

static void draw_border(unsigned char c1, unsigned char c2) {
    ugfx_buf_hline(buf, W, H, 0, 0, W, c1);
    ugfx_buf_hline(buf, W, H, 0, H - 1, W, c2);
    for (int y = 0; y < H; y++) {
        ugfx_buf_pixel(buf, W, H, 0, y, c1);
        ugfx_buf_pixel(buf, W, H, W - 1, y, c2);
    }
}

static void draw_plasma(unsigned int t, int rainbow) {
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            unsigned int v = (x * 3u + y * 5u + t * 7u);
            v ^= (x * y + t * 11u);
            unsigned char c;
            if (rainbow) {
                c = (unsigned char)(1 + (v % 15u));
            } else {
                c = (unsigned char)(9 + (v % 6u));
            }
            buf[y * W + x] = c;
        }
    }
}

static void draw_ui(const char *line, int rainbow, int miracle,
                    int oracle_idx) {
    ugfx_buf_rect(buf, W, H, 8, 8, W - 16, 54, 0);
    ugfx_buf_string(buf, W, H, 16, 14, "wintempleos.wlf", 15);
    ugfx_buf_string(buf, W, H, 16, 26, line, 14);

    ugfx_buf_rect(buf, W, H, 8, H - 26, W - 16, 18, 0);
    ugfx_buf_string(buf, W, H, 12, H - 22,
                    "Q/Esc quit  H oracle  R rainbow  M miracle", 7);

    if (rainbow)
        ugfx_buf_string(buf, W, H, W - 116, 14, "RAINBOW", 10);
    if (miracle)
        ugfx_buf_string(buf, W, H, W - 116, 26, "MIRACLE", 12);

    ugfx_buf_rect(buf, W, H, 8, 70, W - 16, 24, 0);
    ugfx_buf_string(buf, W, H, 16, 76, "oracle #", 11);
    char nbuf[16];
    itoa(oracle_idx + 1, nbuf);
    ugfx_buf_string(buf, W, H, 80, 76, nbuf, 15);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int wid = win_create(W, H, "TempleOS-ish");
    if (wid < 0) {
        print("error: requires window manager\n");
        exit(1);
    }
    detach();

    int rainbow = 0;
    int miracle = 0;
    int oracle_idx = 0;
    unsigned int tick = 0;

    while (1) {
        int k = win_getkey(wid);
        if (k == 27 || k == 'q' || k == 'Q')
            break;
        if (k == 'r' || k == 'R')
            rainbow = !rainbow;
        if (k == 'm' || k == 'M')
            miracle = !miracle;
        if (k == 'h' || k == 'H') {
            oracle_idx = (int)(rng_next() % (sizeof(oracle_lines) /
                                             sizeof(oracle_lines[0])));
        }

        draw_plasma(tick, rainbow);

        unsigned char c1 = (unsigned char)(1 + ((tick / 2u) % 14u));
        unsigned char c2 = (unsigned char)(1 + ((tick / 3u + 5u) % 14u));
        if (miracle) {
            c1 = (unsigned char)(1 + (rng_next() % 15u));
            c2 = (unsigned char)(1 + (rng_next() % 15u));
        }
        draw_border(c1, c2);
        draw_ui(oracle_lines[oracle_idx], rainbow, miracle, oracle_idx);

        if (miracle) {
            ugfx_buf_rect(buf, W, H, 340, 70, 150, 58, 0);
            ugfx_buf_string(buf, W, H, 348, 76, "MIRACLE BENCH", 15);
            ugfx_buf_string(buf, W, H, 348, 88, "FAITH: 100%", 10);
            ugfx_buf_string(buf, W, H, 348, 100, "BLESSED OPS:", 14);
            char pbuf[16];
            itoa((int)(rng_next() % 9000u + 1000u), pbuf);
            ugfx_buf_string(buf, W, H, 432, 100, pbuf, 15);
        }

        win_write(wid, buf, sizeof(buf));
        sleep_ms(33);
        tick++;
    }

    win_destroy(wid);
    exit(0);
}
