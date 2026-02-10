// Window hello app - simple windowed program for mateOS WM

#include "ugfx.h"
#include "syscalls.h"

#define W 500
#define H 350

static unsigned char buf[W * H];
static char last_key_str[] = "Key: _";

void _start(void) {
    int wid = win_create(W, H, "Hello");
    if (wid < 0) {
        exit(1);
    }

    // Draw initial content
    ugfx_buf_clear(buf, W, H, 7);  // Light gray background

    // Decorative border
    ugfx_buf_hline(buf, W, H, 0, 0, W, 9);       // Top - light blue
    ugfx_buf_hline(buf, W, H, 0, H - 1, W, 9);   // Bottom
    for (int y = 0; y < H; y++) {
        ugfx_buf_pixel(buf, W, H, 0, y, 9);       // Left
        ugfx_buf_pixel(buf, W, H, W - 1, y, 9);   // Right
    }

    // Title
    ugfx_buf_string(buf, W, H, 20, 10, "Hello from Window!", 1);
    ugfx_buf_string(buf, W, H, 8, 28, "I'm a windowed app!", 0);
    ugfx_buf_string(buf, W, H, 8, 44, "Press keys to see them", 8);
    ugfx_buf_string(buf, W, H, 8, 56, "Press 'q' to quit", 4);

    win_write(wid, buf, sizeof(buf));

    // Event loop
    while (1) {
        int key = win_getkey(wid);
        if (key == 'q') break;
        if (key > 0) {
            // Show the last pressed key
            last_key_str[5] = (char)key;
            ugfx_buf_rect(buf, W, H, 8, 66, 100, 10, 7);  // Clear area
            ugfx_buf_string(buf, W, H, 8, 66, last_key_str, 4);
            win_write(wid, buf, sizeof(buf));
        }
        yield();
    }

    win_destroy(wid);
    exit(0);
}
