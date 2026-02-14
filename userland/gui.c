// mateOS Window Manager
// Compositing WM that owns the framebuffer, spawns child apps in windows

#include "ugfx.h"
#include "syscalls.h"

// Layout constants (dynamic parts computed from screen size)
#define TASKBAR_H    20
#define TITLE_BAR_H  14
#define BORDER       1
#define WM_MAX_SLOTS 4
#define GAP          6

// Window content size — computed at startup
static int content_w = 148;
static int content_h = 78;

// Colors (CGA palette indices)
#define COL_DESKTOP    1   // Blue
#define COL_TASKBAR    8   // Dark gray
#define COL_TASKBAR_TXT 15 // White
#define COL_TITLE_ACT  9   // Light blue
#define COL_TITLE_INACT 8  // Dark gray
#define COL_TITLE_TXT  15  // White
#define COL_BORDER_ACT 15  // White
#define COL_BORDER_INACT 7 // Light gray
#define COL_CURSOR     15  // White

// Window slot - WM-side tracking
typedef struct {
    int x, y;       // Content area position on screen
    int wid;        // Kernel window ID (-1 if empty)
    int pid;        // Child process PID
    int w, h;       // Window content size
    char title[32];
} wm_slot_t;

static wm_slot_t slots[WM_MAX_SLOTS];
static int focus = 0;
static int num_slots = 0;

// Mouse state
static int drag_slot = -1;   // Slot being dragged (-1 = none)
static int drag_ox, drag_oy; // Offset from slot.x/y to mouse at drag start
static unsigned char prev_buttons = 0;

// Buffer for reading window content (max 500*400 = 200000)
static unsigned char read_buf[200000];

// Cursor bitmap (8x16, 1=white, 0 in mask=transparent)
#define CURSOR_W 8
#define CURSOR_H 16
static const unsigned char cursor_data[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xF8, 0xF8, 0xFC, 0x4C, 0x0C, 0x06, 0x06, 0x00
};
static const unsigned char cursor_mask[CURSOR_H] = {
    0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF,
    0xFF, 0xFC, 0xFE, 0xFE, 0x4E, 0x0F, 0x0F, 0x07
};

// Save buffer for pixels under cursor
static unsigned char cursor_save[CURSOR_W * CURSOR_H];
static int cursor_save_x = -1, cursor_save_y = -1;
static int cursor_visible = 0;

static void hide_cursor(void) {
    if (!cursor_visible) return;
    for (int row = 0; row < CURSOR_H; row++) {
        if (cursor_save_y + row < 0 || cursor_save_y + row >= ugfx_height) continue;
        unsigned char mask = cursor_mask[row];
        for (int col = 0; col < CURSOR_W; col++) {
            if (cursor_save_x + col < 0 || cursor_save_x + col >= ugfx_width) continue;
            if (mask & (0x80 >> col)) {
                ugfx_pixel(cursor_save_x + col, cursor_save_y + row,
                           cursor_save[row * CURSOR_W + col]);
            }
        }
    }
    cursor_visible = 0;
}

static void show_cursor(int mx, int my) {
    // Save pixels under new position
    cursor_save_x = mx;
    cursor_save_y = my;
    for (int row = 0; row < CURSOR_H; row++) {
        if (my + row < 0 || my + row >= ugfx_height) continue;
        unsigned char mask = cursor_mask[row];
        for (int col = 0; col < CURSOR_W; col++) {
            if (mx + col < 0 || mx + col >= ugfx_width) continue;
            if (mask & (0x80 >> col)) {
                cursor_save[row * CURSOR_W + col] =
                    ugfx_read_pixel(mx + col, my + row);
            }
        }
    }
    // Draw cursor
    for (int row = 0; row < CURSOR_H; row++) {
        if (my + row < 0 || my + row >= ugfx_height) continue;
        unsigned char mask = cursor_mask[row];
        unsigned char bits = cursor_data[row];
        for (int col = 0; col < CURSOR_W; col++) {
            if (mx + col < 0 || mx + col >= ugfx_width) continue;
            if (mask & (0x80 >> col)) {
                unsigned char color = (bits & (0x80 >> col)) ? COL_CURSOR : 0;
                ugfx_pixel(mx + col, my + row, color);
            }
        }
    }
    cursor_visible = 1;
}

static void wm_strcpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int find_slot_by_wid(int wid) {
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slots[s].wid == wid) return s;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slots[s].wid < 0) return s;
    }
    return -1;
}

static int next_active_slot(int start) {
    for (int i = 1; i <= WM_MAX_SLOTS; i++) {
        int s = (start + i) % WM_MAX_SLOTS;
        if (slots[s].wid >= 0) return s;
    }
    return -1;
}

static void get_slot_frame(int slot, int *fx, int *fy, int *fw, int *fh) {
    int win_w = slots[slot].w > 0 ? slots[slot].w : content_w;
    int win_h = slots[slot].h > 0 ? slots[slot].h : content_h;
    if (win_w > content_w) win_w = content_w;
    if (win_h > content_h) win_h = content_h;
    *fx = slots[slot].x - BORDER;
    *fy = slots[slot].y - TITLE_BAR_H - BORDER;
    *fw = win_w + 2 * BORDER;
    *fh = win_h + TITLE_BAR_H + 2 * BORDER;
}

// Hit-test: is (mx, my) inside slot's frame?
static int slot_hit_test(int slot, int mx, int my) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    return (mx >= fx && mx < fx + fw && my >= fy && my < fy + fh);
}

// Is (mx, my) in slot's title bar?
static int title_hit_test(int slot, int mx, int my) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    return (mx >= fx && mx < fx + fw &&
            my >= fy && my < fy + TITLE_BAR_H + 2 * BORDER);
}

static void compute_layout(void) {
    // 2x2 grid with gaps
    int usable_w = ugfx_width - GAP * 3;    // left gap + middle gap + right gap
    int usable_h = ugfx_height - TASKBAR_H - GAP * 3;  // top gap + middle + bottom

    content_w = usable_w / 2;
    content_h = (usable_h - (TITLE_BAR_H + 2 * BORDER) * 2) / 2;

    // Clamp to window system limits and child app window size
    if (content_w > 500) content_w = 500;
    if (content_h > 350) content_h = 350;

    // Compute slot positions (content area top-left)
    int frame_h = content_h + TITLE_BAR_H + 2 * BORDER;
    int x0 = GAP + BORDER;
    int x1 = GAP + content_w + 2 * BORDER + GAP + BORDER;
    int y0 = TASKBAR_H + GAP + BORDER + TITLE_BAR_H;
    int y1 = y0 + frame_h + GAP;

    slots[0].x = x0; slots[0].y = y0;
    slots[1].x = x1; slots[1].y = y0;
    slots[2].x = x0; slots[2].y = y1;
    slots[3].x = x1; slots[3].y = y1;
}

// Draw the taskbar
static void draw_taskbar(void) {
    ugfx_rect(0, 0, ugfx_width, TASKBAR_H, COL_TASKBAR);
    ugfx_string(8, 6, "mateOS WM", COL_TASKBAR_TXT);

    // Show focused window title
    if (num_slots > 0 && slots[focus].wid >= 0) {
        ugfx_string(160, 6, slots[focus].title, 14);
    }

    // Hints on right side
    ugfx_string(ugfx_width - 80, 6, "Tab Esc", 7);
}

// Draw a window frame (border + title bar)
static void draw_window_frame(int slot) {
    int is_focused = (slot == focus);
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);

    int win_w = fw - 2 * BORDER;

    // Border
    ugfx_rect_outline(fx, fy, fw, fh,
                      is_focused ? COL_BORDER_ACT : COL_BORDER_INACT);

    // Title bar background
    ugfx_rect(fx + BORDER, fy + BORDER, win_w, TITLE_BAR_H,
              is_focused ? COL_TITLE_ACT : COL_TITLE_INACT);

    // Title text
    if (slots[slot].wid >= 0) {
        ugfx_string(fx + BORDER + 4, fy + BORDER + 3,
                    slots[slot].title, COL_TITLE_TXT);
    }
}

// Clear a slot's screen area to desktop color
static void clear_slot_area(int slot) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    ugfx_rect(fx, fy, fw, fh, COL_DESKTOP);
}

// Blit a window's content buffer onto the framebuffer
static void composite_window(int slot) {
    if (slots[slot].wid < 0) return;

    int win_w = slots[slot].w > 0 ? slots[slot].w : content_w;
    int win_h = slots[slot].h > 0 ? slots[slot].h : content_h;
    if (win_w > content_w) win_w = content_w;
    if (win_h > content_h) win_h = content_h;

    int buf_size = win_w * win_h;
    if (buf_size > (int)sizeof(read_buf)) buf_size = (int)sizeof(read_buf);

    int bytes = win_read(slots[slot].wid, read_buf, (unsigned int)buf_size);
    if (bytes <= 0) {
        // Window was destroyed — clear slot immediately
        clear_slot_area(slot);
        slots[slot].wid = -1;
        slots[slot].pid = -1;
        slots[slot].w = 0;
        slots[slot].h = 0;
        slots[slot].title[0] = '\0';
        return;
    }

    int sx = slots[slot].x;
    int sy = slots[slot].y;

    for (int row = 0; row < win_h; row++) {
        for (int col = 0; col < win_w; col++) {
            int idx = row * win_w + col;
            unsigned char c = (idx < bytes) ? read_buf[idx] : 0;
            ugfx_pixel(sx + col, sy + row, c);
        }
    }
}

// Discover windows created by children
static void discover_windows(void) {
    win_info_t info[8];
    int wcount = win_list(info, 8);
    // Remove slots whose windows no longer exist
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slots[s].wid < 0) continue;
        int alive = 0;
        for (int i = 0; i < wcount; i++) {
            if (info[i].window_id == slots[s].wid) {
                alive = 1;
                break;
            }
        }
        if (!alive) {
            clear_slot_area(s);
            slots[s].wid = -1;
            slots[s].pid = -1;
            slots[s].w = 0;
            slots[s].h = 0;
            slots[s].title[0] = '\0';
        }
    }

    // Add/update current windows
    for (int i = 0; i < wcount; i++) {
        int s = find_slot_by_wid(info[i].window_id);
        if (s >= 0) {
            // Already tracked — just update metadata
            wm_strcpy(slots[s].title, info[i].title, 32);
            slots[s].w = info[i].w;
            slots[s].h = info[i].h;
            continue;
        }

        // New window — find a slot for it
        int is_new = 1;

        // Prefer slot pre-reserved for this pid
        for (int k = 0; k < WM_MAX_SLOTS; k++) {
            if (slots[k].pid == (int)info[i].owner_pid && slots[k].wid < 0) {
                slots[k].wid = info[i].window_id;
                wm_strcpy(slots[k].title, info[i].title, 32);
                slots[k].w = info[i].w;
                slots[k].h = info[i].h;
                s = k;
                // Pre-reserved slots are expected (initial spawn), don't auto-focus
                is_new = 0;
                break;
            }
        }

        // Otherwise adopt into any free slot
        if (s < 0) {
            int free = find_free_slot();
            if (free >= 0) {
                slots[free].pid = (int)info[i].owner_pid;
                slots[free].wid = info[i].window_id;
                wm_strcpy(slots[free].title, info[i].title, 32);
                slots[free].w = info[i].w;
                slots[free].h = info[i].h;
                s = free;
            }
        }

        // Auto-focus genuinely new windows (not pre-reserved ones)
        if (s >= 0 && is_new) {
            focus = s;
        }
    }

    // Recompute number of active slots
    num_slots = 0;
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slots[s].wid >= 0) num_slots++;
    }

    if (num_slots == 0) {
        focus = 0;
    } else if (slots[focus].wid < 0) {
        int nf = next_active_slot(focus);
        if (nf >= 0) focus = nf;
    }
}

// Handle mouse input: click-to-focus and title bar dragging
static void handle_mouse(int mx, int my, unsigned char buttons) {
    int left = buttons & 1;
    int prev_left = prev_buttons & 1;

    // Left button just pressed
    if (left && !prev_left) {
        // Check title bar drag start (check focused window first, then others)
        for (int i = 0; i < WM_MAX_SLOTS; i++) {
            if (slots[i].wid < 0) continue;
            if (title_hit_test(i, mx, my)) {
                drag_slot = i;
                drag_ox = mx - slots[i].x;
                drag_oy = my - slots[i].y;
                focus = i;
                prev_buttons = buttons;
                return;
            }
        }
        // Click-to-focus (not on title bar)
        for (int i = 0; i < WM_MAX_SLOTS; i++) {
            if (slots[i].wid < 0) continue;
            if (slot_hit_test(i, mx, my)) {
                focus = i;
                break;
            }
        }
    }

    // Dragging
    if (left && drag_slot >= 0) {
        int old_x = slots[drag_slot].x;
        int old_y = slots[drag_slot].y;
        int new_x = mx - drag_ox;
        int new_y = my - drag_oy;
        if (new_x != old_x || new_y != old_y) {
            // Clear old position
            clear_slot_area(drag_slot);
            slots[drag_slot].x = new_x;
            slots[drag_slot].y = new_y;
        }
    }

    // Left button released
    if (!left && prev_left) {
        drag_slot = -1;
    }

    prev_buttons = buttons;
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    // Enter graphics mode
    if (ugfx_init() != 0) {
        write(1, "WM: gfx_init failed\n", 20);
        exit(1);
    }

    // Compute layout based on screen dimensions
    compute_layout();

    // Clear to desktop color
    ugfx_clear(COL_DESKTOP);

    // Initialize slots
    for (int i = 0; i < WM_MAX_SLOTS; i++) {
        slots[i].wid = -1;
        slots[i].pid = -1;
        slots[i].w = 0;
        slots[i].h = 0;
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
    int mx = 0, my = 0;
    unsigned char btns = 0;

    while (running) {
        // Keyboard input
        unsigned char key = ugfx_getkey();
        if (key) {
            if (key == 27) {
                running = 0;
            } else if (key == '\t') {
                if (num_slots > 0) {
                    int nf = next_active_slot(focus);
                    if (nf >= 0) focus = nf;
                }
            } else {
                if (num_slots > 0 && slots[focus].wid >= 0) {
                    win_sendkey(slots[focus].wid, key);
                }
            }
        }

        // Mouse input
        getmouse(&mx, &my, &btns);
        handle_mouse(mx, my, btns);

        tick++;
        if (tick % 10 == 0) {
            discover_windows();
        }

        for (int i = 0; i < WM_MAX_SLOTS; i++) {
            if (slots[i].wid >= 0) {
                draw_window_frame(i);
                composite_window(i);
            }
        }

        draw_taskbar();

        // Restore pixels under old cursor, draw at new position
        hide_cursor();
        show_cursor(mx, my);
        yield();
    }

    ugfx_exit();
    exit(0);
}
