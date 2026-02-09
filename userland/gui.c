// mateOS Window Manager
// Compositing WM that owns VGA Mode 13h, spawns child apps in windows

#include "ugfx.h"
#include "syscalls.h"

// Layout constants
#define TASKBAR_H    14
#define TITLE_BAR_H  12
#define BORDER       1
#define CONTENT_W    148
#define CONTENT_H    78
#define WM_MAX_SLOTS 4

// Colors (CGA palette indices)
#define COL_DESKTOP    1   // Blue
#define COL_TASKBAR    8   // Dark gray
#define COL_TASKBAR_TXT 15 // White
#define COL_TITLE_ACT  9   // Light blue
#define COL_TITLE_INACT 8  // Dark gray
#define COL_TITLE_TXT  15  // White
#define COL_BORDER_ACT 15  // White
#define COL_BORDER_INACT 7 // Light gray

// Window slot - WM-side tracking
typedef struct {
    int x, y;       // Content area position on screen
    int wid;        // Kernel window ID (-1 if empty)
    int pid;        // Child process PID
    char title[32];
} wm_slot_t;

static wm_slot_t slots[WM_MAX_SLOTS];
static int focus = 0;
static int num_slots = 0;

// Slot positions (content area top-left)
static const int slot_x[] = {3, 165, 3, 165};
static const int slot_y[] = {
    TASKBAR_H + 2 + BORDER + TITLE_BAR_H,
    TASKBAR_H + 2 + BORDER + TITLE_BAR_H,
    TASKBAR_H + 2 + BORDER + TITLE_BAR_H + CONTENT_H + TITLE_BAR_H + 2 * BORDER + 4,
    TASKBAR_H + 2 + BORDER + TITLE_BAR_H + CONTENT_H + TITLE_BAR_H + 2 * BORDER + 4
};

// Buffer for reading window content
static unsigned char read_buf[CONTENT_W * CONTENT_H];

static void wm_strcpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

// Draw the taskbar
static void draw_taskbar(void) {
    ugfx_rect(0, 0, 320, TASKBAR_H, COL_TASKBAR);
    ugfx_string(4, 3, "mateOS WM", COL_TASKBAR_TXT);

    // Show focused window title on right side
    if (num_slots > 0 && slots[focus].wid >= 0) {
        ugfx_string(120, 3, slots[focus].title, 14); // Yellow
    }

    // Tab/Esc hints
    ugfx_string(220, 3, "Tab Esc", 7);
}

// Draw a window frame (border + title bar)
static void draw_window_frame(int slot) {
    int is_focused = (slot == focus);
    int fx = slot_x[slot] - BORDER;
    int fy = slot_y[slot] - TITLE_BAR_H - BORDER;
    int fw = CONTENT_W + 2 * BORDER;
    int fh = CONTENT_H + TITLE_BAR_H + 2 * BORDER;

    // Border
    ugfx_rect_outline(fx, fy, fw, fh,
                      is_focused ? COL_BORDER_ACT : COL_BORDER_INACT);

    // Title bar background
    ugfx_rect(fx + BORDER, fy + BORDER, CONTENT_W, TITLE_BAR_H,
              is_focused ? COL_TITLE_ACT : COL_TITLE_INACT);

    // Title text
    if (slots[slot].wid >= 0) {
        ugfx_string(fx + BORDER + 4, fy + BORDER + 2,
                    slots[slot].title, COL_TITLE_TXT);
    }
}

// Blit a window's content buffer onto the framebuffer
static void composite_window(int slot) {
    if (slots[slot].wid < 0) return;

    int bytes = win_read(slots[slot].wid, read_buf, sizeof(read_buf));
    if (bytes <= 0) return;

    int sx = slot_x[slot];
    int sy = slot_y[slot];

    for (int row = 0; row < CONTENT_H; row++) {
        for (int col = 0; col < CONTENT_W; col++) {
            ugfx_pixel(sx + col, sy + row,
                       read_buf[row * CONTENT_W + col]);
        }
    }
}

// Discover windows created by children
static void discover_windows(void) {
    win_info_t info[8];
    int wcount = win_list(info, 8);

    for (int i = 0; i < wcount; i++) {
        for (int s = 0; s < num_slots; s++) {
            if ((int)info[i].owner_pid == slots[s].pid && slots[s].wid < 0) {
                slots[s].wid = info[i].window_id;
                wm_strcpy(slots[s].title, info[i].title, 32);
            }
        }
    }
}

void _start(void) {
    // Enter graphics mode
    if (ugfx_init() != 0) {
        write(1, "WM: gfx_init failed\n", 20);
        exit(1);
    }

    // Clear to desktop color
    ugfx_clear(COL_DESKTOP);

    // Initialize slots
    for (int i = 0; i < WM_MAX_SLOTS; i++) {
        slots[i].wid = -1;
        slots[i].pid = -1;
        slots[i].title[0] = '\0';
    }

    // Spawn child apps
    int pid0 = spawn("winterm.elf");
    if (pid0 >= 0) {
        slots[0].pid = pid0;
        wm_strcpy(slots[0].title, "Term 1", 32);
        num_slots = 1;
    }

    int pid1 = spawn("winterm.elf");
    if (pid1 >= 0) {
        slots[1].pid = pid1;
        wm_strcpy(slots[1].title, "Term 2", 32);
        num_slots = 2;
    }

    // Give children time to call win_create
    for (int i = 0; i < 30; i++) yield();

    // Discover their window IDs
    discover_windows();

    // Main loop
    int running = 1;
    int tick = 0;

    while (running) {
        // Read keyboard (WM owns the global buffer)
        unsigned char key = ugfx_getkey();
        if (key) {
            if (key == 27) {
                // ESC: exit WM
                running = 0;
            } else if (key == '\t') {
                // Tab: cycle focus
                if (num_slots > 0) {
                    focus = (focus + 1) % num_slots;
                }
            } else {
                // Route key to focused window
                if (num_slots > 0 && slots[focus].wid >= 0) {
                    win_sendkey(slots[focus].wid, key);
                }
            }
        }

        // Periodically re-discover windows
        tick++;
        if (tick % 50 == 0) {
            discover_windows();
        }

        // Composite all windows
        for (int i = 0; i < num_slots; i++) {
            if (slots[i].wid >= 0) {
                draw_window_frame(i);
                composite_window(i);
            }
        }

        // Draw taskbar (on top)
        draw_taskbar();

        yield();
    }

    // Cleanup
    ugfx_exit();
    exit(0);
}
