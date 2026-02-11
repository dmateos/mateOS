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

// Buffer for reading window content (max 500*400 = 200000)
static unsigned char read_buf[200000];

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
    int win_w = slots[slot].w > 0 ? slots[slot].w : content_w;
    int win_h = slots[slot].h > 0 ? slots[slot].h : content_h;
    if (win_w > content_w) win_w = content_w;
    if (win_h > content_h) win_h = content_h;
    int fx = slots[slot].x - BORDER;
    int fy = slots[slot].y - TITLE_BAR_H - BORDER;
    int fw = win_w + 2 * BORDER;
    int fh = win_h + TITLE_BAR_H + 2 * BORDER;

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
    if (bytes <= 0) return;

    int sx = slots[slot].x;
    int sy = slots[slot].y;

    for (int row = 0; row < win_h; row++) {
        for (int col = 0; col < win_w; col++) {
            ugfx_pixel(sx + col, sy + row,
                       read_buf[row * win_w + col]);
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
            wm_strcpy(slots[s].title, info[i].title, 32);
            slots[s].w = info[i].w;
            slots[s].h = info[i].h;
            continue;
        }

        // Prefer slot pre-reserved for this pid
        for (int k = 0; k < WM_MAX_SLOTS; k++) {
            if (slots[k].pid == (int)info[i].owner_pid && slots[k].wid < 0) {
                slots[k].wid = info[i].window_id;
                wm_strcpy(slots[k].title, info[i].title, 32);
                slots[k].w = info[i].w;
                slots[k].h = info[i].h;
                s = k;
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
            }
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

void _start(void) {
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

    while (running) {
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

        tick++;
        if (tick % 50 == 0) {
            discover_windows();
        }

        for (int i = 0; i < WM_MAX_SLOTS; i++) {
            if (slots[i].wid >= 0) {
                draw_window_frame(i);
                composite_window(i);
            }
        }

        draw_taskbar();
        yield();
    }

    ugfx_exit();
    exit(0);
}
