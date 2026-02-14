// mateOS Window Manager
// Compositing WM with overlap support and full-frame backbuffer present.

#include "ugfx.h"
#include "syscalls.h"
#include "libc.h"

#define TASKBAR_H    20
#define TITLE_BAR_H  14
#define BORDER       1
#define WM_MAX_SLOTS 16
#define GAP          6
#define CLOSE_W      12

#define MAX_FB_W 1024
#define MAX_FB_H 768

// Colors (CGA-like palette)
#define COL_DESKTOP      1
#define COL_TASKBAR      8
#define COL_TASKBAR_TXT 15
#define COL_TITLE_ACT    9
#define COL_TITLE_INACT  8
#define COL_TITLE_TXT   15
#define COL_BORDER_ACT  15
#define COL_BORDER_INACT 7
#define COL_CURSOR      15

#define DS_ICON_W    40
#define DS_ICON_H    44
#define DS_TERM_X    12
#define DS_TERM_Y    (TASKBAR_H + 10)
#define DS_FILES_X   12
#define DS_FILES_Y   (TASKBAR_H + 60)
#define DS_TASK_X    12
#define DS_TASK_Y    (TASKBAR_H + 110)

typedef struct {
    int x, y;       // content area top-left
    int wid;        // kernel window id
    int pid;        // child pid
    int w, h;       // content dimensions
    char title[32];
} wm_slot_t;

static wm_slot_t slots[WM_MAX_SLOTS];
static int focus = 0;
static int num_slots = 0;

// z_order holds active slot indices from back to front.
static int z_order[WM_MAX_SLOTS];
static int z_count = 0;

// Dynamic content size from display resolution.
static int content_w = 148;
static int content_h = 78;
static unsigned int place_seed = 0xC0FFEE11u;

// Mouse state
static int drag_slot = -1;
static int drag_ox = 0, drag_oy = 0;
static unsigned char prev_buttons = 0;

// Read buffer for child window pixels
static unsigned char read_buf[200000];

// Full-screen compositor backbuffer
static unsigned char wm_backbuf[MAX_FB_W * MAX_FB_H];

// Cursor bitmap (8x16)
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

static void wm_strcpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int slot_is_active(int s) {
    return (s >= 0 && s < WM_MAX_SLOTS && slots[s].wid >= 0);
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

static void z_sync_active(void) {
    int next[WM_MAX_SLOTS];
    int n = 0;

    // Keep existing ordering for still-active slots.
    for (int i = 0; i < z_count; i++) {
        int s = z_order[i];
        if (slot_is_active(s)) next[n++] = s;
    }

    // Append any active slot that was not already tracked.
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (!slot_is_active(s)) continue;
        int seen = 0;
        for (int i = 0; i < n; i++) {
            if (next[i] == s) { seen = 1; break; }
        }
        if (!seen) next[n++] = s;
    }

    for (int i = 0; i < n; i++) z_order[i] = next[i];
    z_count = n;
}

static void z_bring_front(int slot) {
    if (!slot_is_active(slot)) return;
    z_sync_active();

    int out = 0;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] != slot) z_order[out++] = z_order[i];
    }
    z_order[out++] = slot;
    z_count = out;
}

static int z_next_focus(void) {
    if (z_count <= 0) return -1;
    // Cycle through z-order slots.
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == focus) {
            return z_order[(i + 1) % z_count];
        }
    }
    return z_order[z_count - 1];
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

static int slot_hit_test(int slot, int mx, int my) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    return (mx >= fx && mx < fx + fw && my >= fy && my < fy + fh);
}

static int title_hit_test(int slot, int mx, int my) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    return (mx >= fx && mx < fx + fw && my >= fy && my < fy + TITLE_BAR_H + 2 * BORDER);
}

static int close_hit_test(int slot, int mx, int my) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    int bx = fx + fw - BORDER - CLOSE_W - 2;
    int by = fy + BORDER + 1;
    int bw = CLOSE_W;
    int bh = TITLE_BAR_H - 2;
    return (mx >= bx && mx < bx + bw && my >= by && my < by + bh);
}

static void compute_layout(void) {
    // Keep the original larger 2x2-era content sizing, even though we now
    // support more windows and overlap via z-order.
    int usable_w = ugfx_width - GAP * 3;
    int usable_h = ugfx_height - TASKBAR_H - GAP * 3;
    content_w = usable_w / 2;
    content_h = (usable_h - (TITLE_BAR_H + 2 * BORDER) * 2) / 2;
    if (content_w > 500) content_w = 500;
    if (content_h > 350) content_h = 350;
    if (content_w < 120) content_w = 120;
    if (content_h < 72) content_h = 72;
}

static unsigned int next_place_rand(void) {
    place_seed = place_seed * 1664525u + 1013904223u;
    return place_seed;
}

static void place_slot_random(int slot, unsigned int salt) {
    int min_x = GAP + BORDER;
    int max_x = ugfx_width - GAP - BORDER - content_w;
    int min_y = TASKBAR_H + GAP + BORDER + TITLE_BAR_H;
    int max_y = ugfx_height - GAP - BORDER - content_h;

    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    place_seed ^= salt + (unsigned int)(slot * 2654435761u);
    unsigned int rx = next_place_rand();
    unsigned int ry = next_place_rand();

    int span_x = (max_x - min_x) + 1;
    int span_y = (max_y - min_y) + 1;
    slots[slot].x = min_x + (int)(rx % (unsigned int)span_x);
    slots[slot].y = min_y + (int)(ry % (unsigned int)span_y);
}

static inline void bb_pixel(int x, int y, unsigned char c) {
    if (x < 0 || x >= ugfx_width || y < 0 || y >= ugfx_height) return;
    wm_backbuf[y * ugfx_width + x] = c;
}

static void bb_clear(unsigned char c) {
    for (int i = 0; i < ugfx_width * ugfx_height; i++) wm_backbuf[i] = c;
}

static void bb_rect(int x, int y, int w, int h, unsigned char c) {
    ugfx_buf_rect(wm_backbuf, ugfx_width, ugfx_height, x, y, w, h, c);
}

static void bb_hline(int x, int y, int w, unsigned char c) {
    ugfx_buf_hline(wm_backbuf, ugfx_width, ugfx_height, x, y, w, c);
}

static void bb_vline(int x, int y, int h, unsigned char c) {
    for (int i = 0; i < h; i++) bb_pixel(x, y + i, c);
}

static void bb_rect_outline(int x, int y, int w, int h, unsigned char c) {
    bb_hline(x, y, w, c);
    bb_hline(x, y + h - 1, w, c);
    bb_vline(x, y, h, c);
    bb_vline(x + w - 1, y, h, c);
}

static void bb_string(int x, int y, const char *s, unsigned char c) {
    ugfx_buf_string(wm_backbuf, ugfx_width, ugfx_height, x, y, s, c);
}

static int hit_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

static void launch_term(void) {
    int pid = spawn("winterm.elf");
    (void)pid;
}

static void launch_files(void) {
    int pid = spawn("winfm.elf");
    (void)pid;
}

static void launch_tasks(void) {
    int pid = spawn("wintask.elf");
    (void)pid;
}

static void draw_desktop_icon(int x, int y, unsigned char body, const char *label) {
    bb_rect(x + 6, y, 24, 20, body);
    bb_rect_outline(x + 6, y, 24, 20, 15);
    bb_rect(x + 22, y, 8, 6, 15);
    bb_string(x, y + 26, label, 15);
}

static void draw_desktop_icons(void) {
    draw_desktop_icon(DS_TERM_X, DS_TERM_Y, 2, "TERM");
    draw_desktop_icon(DS_FILES_X, DS_FILES_Y, 3, "FILES");
    draw_desktop_icon(DS_TASK_X, DS_TASK_Y, 6, "TASKS");
}

static void draw_taskbar(void) {
    bb_rect(0, 0, ugfx_width, TASKBAR_H, COL_TASKBAR);
    bb_string(8, 6, "mateOS WM", COL_TASKBAR_TXT);

    if (slot_is_active(focus)) {
        bb_string(160, 6, slots[focus].title, 14);
    }

    bb_string(ugfx_width - 88, 6, "Tab Esc", 7);
}

static void draw_window_frame(int slot, int is_focused) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    int win_w = fw - 2 * BORDER;

    bb_rect_outline(fx, fy, fw, fh, is_focused ? COL_BORDER_ACT : COL_BORDER_INACT);
    bb_rect(fx + BORDER, fy + BORDER, win_w, TITLE_BAR_H,
            is_focused ? COL_TITLE_ACT : COL_TITLE_INACT);

    if (slot_is_active(slot)) {
        bb_string(fx + BORDER + 4, fy + BORDER + 3, slots[slot].title, COL_TITLE_TXT);

        // Close button (Windows-like titlebar X)
        int bx = fx + fw - BORDER - CLOSE_W - 2;
        int by = fy + BORDER + 1;
        int bw = CLOSE_W;
        int bh = TITLE_BAR_H - 2;
        bb_rect(bx, by, bw, bh, is_focused ? 7 : 8);
        bb_rect_outline(bx, by, bw, bh, 15);
        bb_string(bx + 2, by + 2, "X", 0);
    }
}

static void composite_window(int slot) {
    if (!slot_is_active(slot)) return;

    int win_w = slots[slot].w > 0 ? slots[slot].w : content_w;
    int win_h = slots[slot].h > 0 ? slots[slot].h : content_h;
    if (win_w > content_w) win_w = content_w;
    if (win_h > content_h) win_h = content_h;

    int buf_size = win_w * win_h;
    if (buf_size > (int)sizeof(read_buf)) buf_size = (int)sizeof(read_buf);

    int bytes = win_read(slots[slot].wid, read_buf, (unsigned int)buf_size);
    if (bytes <= 0) {
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
        int dy = sy + row;
        if (dy < 0 || dy >= ugfx_height) continue;

        for (int col = 0; col < win_w; col++) {
            int dx = sx + col;
            if (dx < 0 || dx >= ugfx_width) continue;

            int idx = row * win_w + col;
            unsigned char c = (idx < bytes) ? read_buf[idx] : 0;
            bb_pixel(dx, dy, c);
        }
    }
}

static void draw_cursor(int mx, int my) {
    for (int row = 0; row < CURSOR_H; row++) {
        int y = my + row;
        if (y < 0 || y >= ugfx_height) continue;
        unsigned char mask = cursor_mask[row];
        unsigned char bits = cursor_data[row];
        for (int col = 0; col < CURSOR_W; col++) {
            int x = mx + col;
            if (x < 0 || x >= ugfx_width) continue;
            if (mask & (0x80 >> col)) {
                unsigned char color = (bits & (0x80 >> col)) ? COL_CURSOR : 0;
                bb_pixel(x, y, color);
            }
        }
    }
}

static void discover_windows(void) {
    win_info_t info[8];
    int wcount = win_list(info, 8);

    // Remove dead tracked windows.
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (!slot_is_active(s)) continue;
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

    // Add or update windows.
    for (int i = 0; i < wcount; i++) {
        int s = find_slot_by_wid(info[i].window_id);
        if (s >= 0) {
            wm_strcpy(slots[s].title, info[i].title, 32);
            slots[s].w = info[i].w;
            slots[s].h = info[i].h;
            continue;
        }

        int is_new = 1;

        // Prefer pre-reserved slot by pid.
        for (int k = 0; k < WM_MAX_SLOTS; k++) {
            if (slots[k].pid == (int)info[i].owner_pid && slots[k].wid < 0) {
                slots[k].wid = info[i].window_id;
                wm_strcpy(slots[k].title, info[i].title, 32);
                slots[k].w = info[i].w;
                slots[k].h = info[i].h;
                place_slot_random(k, info[i].owner_pid);
                s = k;
                is_new = 0;
                break;
            }
        }

        if (s < 0) {
            int free_slot = find_free_slot();
            if (free_slot >= 0) {
                slots[free_slot].pid = (int)info[i].owner_pid;
                slots[free_slot].wid = info[i].window_id;
                wm_strcpy(slots[free_slot].title, info[i].title, 32);
                slots[free_slot].w = info[i].w;
                slots[free_slot].h = info[i].h;
                place_slot_random(free_slot, info[i].owner_pid);
                s = free_slot;
            }
        }

        if (s >= 0 && is_new) {
            focus = s;
            z_bring_front(s);
        }
    }

    num_slots = 0;
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slot_is_active(s)) num_slots++;
    }

    z_sync_active();

    if (!slot_is_active(focus) && z_count > 0) {
        focus = z_order[z_count - 1];
    }
}

static void handle_mouse(int mx, int my, unsigned char buttons) {
    int left = buttons & 1;
    int prev_left = prev_buttons & 1;

    if (left && !prev_left) {
        int hit_window = 0;
        for (int zi = z_count - 1; zi >= 0; zi--) {
            int s = z_order[zi];
            if (slot_hit_test(s, mx, my)) {
                hit_window = 1;
                break;
            }
        }
        if (!hit_window) {
            if (hit_rect(mx, my, DS_TERM_X, DS_TERM_Y, DS_ICON_W, DS_ICON_H)) {
                launch_term();
                prev_buttons = buttons;
                return;
            }
            if (hit_rect(mx, my, DS_FILES_X, DS_FILES_Y, DS_ICON_W, DS_ICON_H)) {
                launch_files();
                prev_buttons = buttons;
                return;
            }
            if (hit_rect(mx, my, DS_TASK_X, DS_TASK_Y, DS_ICON_W, DS_ICON_H)) {
                launch_tasks();
                prev_buttons = buttons;
                return;
            }
        }

        // Front-to-back close-button hit-test first.
        for (int zi = z_count - 1; zi >= 0; zi--) {
            int s = z_order[zi];
            if (close_hit_test(s, mx, my)) {
                focus = s;
                z_bring_front(s);
                if (slot_is_active(s)) {
                    // Prefer graceful close for detached GUI apps.
                    int rc = win_sendkey(slots[s].wid, 27);
                    if (rc != 0) {
                        rc = win_sendkey(slots[s].wid, (unsigned char)'q');
                    }
                    if (rc != 0) {
                        (void)kill(slots[s].pid);
                    }
                }
                prev_buttons = buttons;
                return;
            }
        }

        // Front-to-back hit-test for title drag start.
        for (int zi = z_count - 1; zi >= 0; zi--) {
            int s = z_order[zi];
            if (title_hit_test(s, mx, my)) {
                drag_slot = s;
                drag_ox = mx - slots[s].x;
                drag_oy = my - slots[s].y;
                focus = s;
                z_bring_front(s);
                prev_buttons = buttons;
                return;
            }
        }

        // Front-to-back click-to-focus.
        for (int zi = z_count - 1; zi >= 0; zi--) {
            int s = z_order[zi];
            if (slot_hit_test(s, mx, my)) {
                focus = s;
                z_bring_front(s);
                break;
            }
        }
    }

    if (left && drag_slot >= 0 && slot_is_active(drag_slot)) {
        slots[drag_slot].x = mx - drag_ox;
        slots[drag_slot].y = my - drag_oy;
    }

    if (!left && prev_left) {
        drag_slot = -1;
    }

    prev_buttons = buttons;
}

static void render_frame(int mx, int my) {
    bb_clear(COL_DESKTOP);
    draw_desktop_icons();

    // Back to front
    for (int i = 0; i < z_count; i++) {
        int s = z_order[i];
        if (!slot_is_active(s)) continue;
        draw_window_frame(s, s == focus);
        composite_window(s);
    }

    draw_taskbar();
    draw_cursor(mx, my);

    ugfx_present(wm_backbuf, ugfx_width, ugfx_height);
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    if (ugfx_init() != 0) {
        write(1, "WM: gfx_init failed\n", 20);
        exit(1);
    }

    if (ugfx_width > MAX_FB_W || ugfx_height > MAX_FB_H) {
        write(1, "WM: unsupported resolution\n", 28);
        ugfx_exit();
        exit(1);
    }

    compute_layout();

    for (int i = 0; i < WM_MAX_SLOTS; i++) {
        slots[i].wid = -1;
        slots[i].pid = -1;
        slots[i].w = 0;
        slots[i].h = 0;
        slots[i].title[0] = '\0';
        place_slot_random(i, (unsigned int)i);
        z_order[i] = i;
    }
    z_count = 0;

    int pid0 = spawn("winterm.elf");
    if (pid0 >= 0) {
        slots[0].pid = pid0;
        wm_strcpy(slots[0].title, "Term 1", 32);
    }

    int pid1 = spawn("winfm.elf");
    if (pid1 >= 0) {
        slots[1].pid = pid1;
        wm_strcpy(slots[1].title, "FileMgr", 32);
    }

    for (int i = 0; i < 30; i++) yield();
    discover_windows();

    int running = 1;
    int tick = 0;
    int mx = 0, my = 0;
    unsigned char btns = 0;
    int last_mx = -1, last_my = -1;
    unsigned char last_btns = 0xFF;
    int need_redraw = 1;

    while (running) {
        unsigned char key = ugfx_getkey();
        if (key) {
            need_redraw = 1;
            if (key == 27) {
                running = 0;
            } else if (key == '\t') {
                int nf = z_next_focus();
                if (nf >= 0) {
                    focus = nf;
                    z_bring_front(nf);
                }
            } else if (slot_is_active(focus)) {
                win_sendkey(slots[focus].wid, key);
            }
        }

        getmouse(&mx, &my, &btns);
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= ugfx_width) mx = ugfx_width - 1;
        if (my >= ugfx_height) my = ugfx_height - 1;

        if (mx != last_mx || my != last_my || btns != last_btns) {
            need_redraw = 1;
            last_mx = mx;
            last_my = my;
            last_btns = btns;
        }

        handle_mouse(mx, my, btns);

        tick++;
        if (tick % 20 == 0) {
            discover_windows();
            need_redraw = 1;
        }

        if (need_redraw) {
            render_frame(mx, my);
            need_redraw = 0;
            yield();
        } else {
            // Idle throttle to reduce busy-loop CPU burn.
            sleep_ms(10);
        }
    }

    ugfx_exit();
    exit(0);
}
