#include "ugfx.h"
#include "syscalls.h"

// Color palette indices (CGA colors)
#define COL_BLACK     0
#define COL_BLUE      1
#define COL_GREEN     2
#define COL_CYAN      3
#define COL_RED       4
#define COL_MAGENTA   5
#define COL_BROWN     6
#define COL_LGRAY     7
#define COL_DGRAY     8
#define COL_LBLUE     9
#define COL_LGREEN   10
#define COL_LCYAN    11
#define COL_LRED     12
#define COL_LMAGENTA 13
#define COL_YELLOW   14
#define COL_WHITE    15

// Button structure
typedef struct {
    int x, y, w, h;
    const char *label;
    unsigned char bg, fg;
    unsigned char bg_sel, fg_sel;
} button_t;

// String length helper
static int slen(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

// Draw a button
static void draw_button(const button_t *btn, int selected) {
    unsigned char bg = selected ? btn->bg_sel : btn->bg;
    unsigned char fg = selected ? btn->fg_sel : btn->fg;
    ugfx_rect(btn->x, btn->y, btn->w, btn->h, bg);
    ugfx_rect_outline(btn->x, btn->y, btn->w, btn->h,
                      selected ? COL_WHITE : COL_DGRAY);
    // Center label
    int tx = btn->x + (btn->w - slen(btn->label) * 8) / 2;
    int ty = btn->y + (btn->h - 8) / 2;
    ugfx_string(tx, ty, btn->label, fg);
}

// Text input state
static char text_buf[26];
static int text_len = 0;

static void draw_text_input(int x, int y, int w, int active) {
    ugfx_rect(x, y, w, 12, COL_WHITE);
    ugfx_rect_outline(x, y, w, 12, active ? COL_LBLUE : COL_DGRAY);
    if (text_len > 0) {
        ugfx_string(x + 2, y + 2, text_buf, COL_BLACK);
    }
    // Cursor
    if (active) {
        int cx = x + 2 + text_len * 8;
        if (cx < x + w - 2) {
            ugfx_vline(cx, y + 2, 8, COL_BLACK);
        }
    }
}

// Draw status bar at bottom
static void draw_status_bar(void) {
    ugfx_rect(0, 191, 320, 9, COL_DGRAY);
    ugfx_string(4, 192, "TAB:next ENTER:press q:quit", COL_WHITE);
}

// Window dimensions
#define WIN_X 20
#define WIN_Y 10
#define WIN_W 280
#define WIN_H 175

// Draw the desktop background (areas around window)
static void draw_desktop(unsigned char color) {
    ugfx_rect(0, 0, SCREEN_WIDTH, WIN_Y, color);
    ugfx_rect(0, WIN_Y, WIN_X, WIN_H, color);
    ugfx_rect(WIN_X + WIN_W, WIN_Y, SCREEN_WIDTH - WIN_X - WIN_W, WIN_H, color);
    ugfx_rect(0, WIN_Y + WIN_H, SCREEN_WIDTH, 191 - WIN_Y - WIN_H, color);
}

// Draw the window frame
static void draw_window(void) {
    // Window background
    ugfx_rect(WIN_X, WIN_Y, WIN_W, WIN_H, COL_LGRAY);

    // Title bar
    ugfx_rect(WIN_X, WIN_Y, WIN_W, 14, COL_BLUE);
    ugfx_string(WIN_X + 4, WIN_Y + 3, "mateOS GUI", COL_WHITE);
    ugfx_string(WIN_X + WIN_W - 24, WIN_Y + 3, "[X]", COL_LRED);

    // Separator
    ugfx_hline(WIN_X, WIN_Y + 14, WIN_W, COL_DGRAY);

    // Welcome text
    ugfx_string(WIN_X + 8, WIN_Y + 20, "Welcome to mateOS!", COL_BLACK);
    ugfx_string(WIN_X + 8, WIN_Y + 32, "Running in Ring 3 (user mode)", COL_DGRAY);
}

void _start(void) {
    // Enter graphics mode
    if (ugfx_init() != 0) {
        const char *msg = "gfx_init failed\n";
        write(1, msg, 16);
        exit(1);
    }

    // Draw initial UI
    unsigned char desktop_color = COL_BLUE;
    ugfx_clear(desktop_color);
    draw_window();

    // Buttons
    button_t buttons[3] = {
        {WIN_X + 10,  WIN_Y + 50, 80, 16, "About",
         COL_LGRAY, COL_BLACK, COL_LBLUE, COL_WHITE},
        {WIN_X + 100, WIN_Y + 50, 80, 16, "Color",
         COL_LGRAY, COL_BLACK, COL_LGREEN, COL_WHITE},
        {WIN_X + 190, WIN_Y + 50, 80, 16, "Quit",
         COL_LGRAY, COL_BLACK, COL_LRED, COL_WHITE},
    };

    // Text input
    text_buf[0] = '\0';
    text_len = 0;
    ugfx_string(WIN_X + 8, WIN_Y + 78, "Type:", COL_BLACK);

    // Focus: 0-2 = buttons, 3 = text input
    int focus = 0;
    int running = 1;
    int color_idx = 0;

    // Initial draw
    for (int i = 0; i < 3; i++) {
        draw_button(&buttons[i], i == focus);
    }
    draw_text_input(WIN_X + 50, WIN_Y + 75, 220, focus == 3);
    draw_status_bar();

    // Message area
    int msg_y = WIN_Y + 100;

    // Main event loop
    while (running) {
        unsigned char key = ugfx_waitkey();

        if (key == 'q' && focus != 3) {
            running = 0;
            continue;
        }

        if (key == '\t') {
            // Redraw old focused element as unfocused
            if (focus < 3) {
                draw_button(&buttons[focus], 0);
            } else {
                draw_text_input(WIN_X + 50, WIN_Y + 75, 220, 0);
            }
            // Advance focus
            focus = (focus + 1) % 4;
            // Draw new focused element
            if (focus < 3) {
                draw_button(&buttons[focus], 1);
            } else {
                draw_text_input(WIN_X + 50, WIN_Y + 75, 220, 1);
            }
        } else if (key == '\n' && focus < 3) {
            // Button press
            if (focus == 0) {
                // About
                ugfx_rect(WIN_X + 4, msg_y, WIN_W - 8, 60, COL_LGRAY);
                ugfx_string(WIN_X + 8, msg_y + 4,  "mateOS v0.1", COL_BLACK);
                ugfx_string(WIN_X + 8, msg_y + 16, "A hobby x86 operating system", COL_DGRAY);
                ugfx_string(WIN_X + 8, msg_y + 28, "VGA 320x200 - Mode 13h", COL_DGRAY);
                ugfx_string(WIN_X + 8, msg_y + 40, "User-mode GUI via syscalls", COL_DGRAY);
            } else if (focus == 1) {
                // Color demo
                ugfx_rect(WIN_X + 4, msg_y, WIN_W - 8, 60, COL_LGRAY);
                // Draw 16 color swatches
                for (int i = 0; i < 16; i++) {
                    ugfx_rect(WIN_X + 8 + i * 16, msg_y + 4, 14, 14,
                              (unsigned char)i);
                }
                ugfx_string(WIN_X + 8, msg_y + 24, "16 CGA palette colors", COL_BLACK);
                // Cycle desktop background
                {
                    static const unsigned char bg_colors[] = {1, 9, 3, 5, 4, 2};
                    color_idx = (color_idx + 1) % 6;
                    desktop_color = bg_colors[color_idx];
                    draw_desktop(desktop_color);
                    draw_status_bar();
                }
                ugfx_string(WIN_X + 8, msg_y + 40, "Desktop color changed!", COL_DGRAY);
            } else if (focus == 2) {
                // Quit
                running = 0;
            }
        } else if (focus == 3) {
            // Text input
            if (key == '\b') {
                if (text_len > 0) {
                    text_len--;
                    text_buf[text_len] = '\0';
                }
            } else if (key >= 32 && key < 127 && text_len < 24) {
                text_buf[text_len++] = (char)key;
                text_buf[text_len] = '\0';
            }
            draw_text_input(WIN_X + 50, WIN_Y + 75, 220, 1);
        }
    }

    // Exit graphics mode and return to console
    ugfx_exit();
    write(1, "GUI exited.\n", 12);
    exit(0);
}
