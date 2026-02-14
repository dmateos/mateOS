// Window text editor - simple text editor for mateOS WM

#include "ugfx.h"
#include "syscalls.h"

#define W 500
#define H 350
#define MAX_TEXT 2000
#define CHARS_PER_LINE (W / 8 - 1)  // ~61 chars per line
#define MAX_LINES ((H - 12) / 10)   // ~33 lines of text

static unsigned char buf[W * H];
static char text[MAX_TEXT];
static int text_len = 0;

static void redraw(void) {
    // White background
    ugfx_buf_clear(buf, W, H, 15);

    // Header bar
    ugfx_buf_rect(buf, W, H, 0, 0, W, 10, 8);  // Dark gray
    ugfx_buf_string(buf, W, H, 4, 1, "Text Editor", 15);

    // Draw text with wrapping
    int x = 4, y = 14;
    for (int i = 0; i < text_len; i++) {
        if (text[i] == '\n' || x + 8 > W - 4) {
            x = 4;
            y += 10;
            if (y + 8 > H) break;
            if (text[i] == '\n') continue;
        }
        ugfx_buf_char(buf, W, H, x, y, text[i], 0);
        x += 8;
    }

    // Cursor (blinking block)
    if (y + 8 <= H) {
        ugfx_buf_rect(buf, W, H, x, y, 7, 8, 0);
    }
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    int wid = win_create(W, H, "Editor");
    if (wid < 0) {
        exit(1);
    }

    text[0] = '\0';
    redraw();
    win_write(wid, buf, sizeof(buf));

    while (1) {
        int key = win_getkey(wid);
        if (key == 27) break;  // ESC to quit
        if (key > 0) {
            int changed = 0;
            if (key == '\b') {
                if (text_len > 0) {
                    text_len--;
                    text[text_len] = '\0';
                    changed = 1;
                }
            } else if (key == '\n') {
                if (text_len < MAX_TEXT - 1) {
                    text[text_len++] = '\n';
                    text[text_len] = '\0';
                    changed = 1;
                }
            } else if (key >= 32 && key < 127) {
                if (text_len < MAX_TEXT - 1) {
                    text[text_len++] = (char)key;
                    text[text_len] = '\0';
                    changed = 1;
                }
            }

            if (changed) {
                redraw();
                win_write(wid, buf, sizeof(buf));
            }
        }
        yield();
    }

    win_destroy(wid);
    exit(0);
}
