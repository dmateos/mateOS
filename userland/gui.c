// mateOS Window Manager
// Compositing WM with overlap support and full-frame backbuffer present.

#include "libc.h"
#include "syscalls.h"
#include "ugfx.h"

#define TASKBAR_H 20
#define TITLE_BAR_H 14
#define BORDER 1
#define WM_MAX_SLOTS 16
#define GAP 6
#define CLOSE_W 12

#define MAX_FB_W 1024
#define MAX_FB_H 768

// Theme colors (indexed palette)
#define COL_DESKTOP_A 31 // deep teal-blue
#define COL_DESKTOP_B 31
#define COL_DESKTOP_DOT 67
#define COL_TASKBAR_BG 24    // dark blue
#define COL_TASKBAR_STRIP 74 // cyan-blue accent
#define COL_TASKBAR_TXT 255
#define COL_TASKBAR_MUTED 250
#define COL_TITLE_ACT_A 75   // bright blue
#define COL_TITLE_ACT_B 117  // lighter cyan-blue
#define COL_TITLE_INACT_A 60 // muted slate
#define COL_TITLE_INACT_B 67
#define COL_TITLE_TXT 255
#define COL_TITLE_TXT_DIM 252
#define COL_BORDER_ACT 254
#define COL_BORDER_INACT 244
#define COL_SURFACE 237 // dark gray surface
#define COL_SURFACE_EDGE 242
#define COL_SHADOW_NEAR 236
#define COL_SHADOW_FAR 0
#define COL_CURSOR 255

#define DS_ICON_W 40
#define DS_ICON_H 44
#define DS_TERM_X 12
#define DS_TERM_Y (TASKBAR_H + 10)
#define DS_FILES_X 12
#define DS_FILES_Y (TASKBAR_H + 60)
#define DS_TASK_X 12
#define DS_TASK_Y (TASKBAR_H + 110)

typedef struct {
    int x, y; // content area top-left
    int wid;  // kernel window id
    int pid;  // child pid
    int w, h; // content dimensions
    char title[32];
} wm_slot_t;

static wm_slot_t slots[WM_MAX_SLOTS];
static int focus = 0;
static int num_slots = 0;
static char g_kversion[96];
static char g_info_lines[6][96];
static int g_info_line_count = 0;
static unsigned int g_last_info_refresh = 0;

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

// Read buffer for child window pixels (large enough for 640x400 Doom and roomy
// apps)
static unsigned char read_buf[800 * 500];

// Full-screen compositor backbuffer
static unsigned char wm_backbuf[MAX_FB_W * MAX_FB_H];

// Cursor bitmap (8x16)
#define CURSOR_W 8
#define CURSOR_H 16
static const unsigned char cursor_data[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xF8, 0xF8, 0xFC, 0x4C, 0x0C, 0x06, 0x06, 0x00};
static const unsigned char cursor_mask[CURSOR_H] = {
    0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF,
    0xFF, 0xFC, 0xFE, 0xFE, 0x4E, 0x0F, 0x0F, 0x07};

static void wm_strcpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int wm_strlen(const char *s) {
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void wm_append_str(char *dst, int max, const char *src) {
    int i = wm_strlen(dst);
    int j = 0;
    while (i < max - 1 && src && src[j])
        dst[i++] = src[j++];
    dst[i] = '\0';
}

static void wm_append_u32(char *dst, int max, unsigned int v) {
    char tmp[16];
    int n = 0;
    if (v == 0) {
        wm_append_str(dst, max, "0");
        return;
    }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        char c[2];
        c[0] = tmp[--n];
        c[1] = '\0';
        wm_append_str(dst, max, c);
    }
}

static void wm_append_2d(char *dst, int max, unsigned int v) {
    char c[3];
    c[0] = (char)('0' + ((v / 10u) % 10u));
    c[1] = (char)('0' + (v % 10u));
    c[2] = '\0';
    wm_append_str(dst, max, c);
}

static int slot_is_active(int s) {
    return (s >= 0 && s < WM_MAX_SLOTS && slots[s].wid >= 0);
}

static int find_slot_by_wid(int wid) {
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slots[s].wid == wid)
            return s;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (slots[s].wid < 0)
            return s;
    }
    return -1;
}

static void z_sync_active(void) {
    int next[WM_MAX_SLOTS];
    int n = 0;

    // Keep existing ordering for still-active slots.
    for (int i = 0; i < z_count; i++) {
        int s = z_order[i];
        if (slot_is_active(s))
            next[n++] = s;
    }

    // Append any active slot that was not already tracked.
    for (int s = 0; s < WM_MAX_SLOTS; s++) {
        if (!slot_is_active(s))
            continue;
        int seen = 0;
        for (int i = 0; i < n; i++) {
            if (next[i] == s) {
                seen = 1;
                break;
            }
        }
        if (!seen)
            next[n++] = s;
    }

    for (int i = 0; i < n; i++)
        z_order[i] = next[i];
    z_count = n;
}

static void z_bring_front(int slot) {
    if (!slot_is_active(slot))
        return;
    z_sync_active();

    int out = 0;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] != slot)
            z_order[out++] = z_order[i];
    }
    z_order[out++] = slot;
    z_count = out;
}

static int z_next_focus(void) {
    if (z_count <= 0)
        return -1;
    // Cycle through z-order slots.
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == focus) {
            return z_order[(i + 1) % z_count];
        }
    }
    return z_order[z_count - 1];
}

static void get_slot_content_size(int slot, int *out_w, int *out_h) {
    int w = slots[slot].w > 0 ? slots[slot].w : content_w;
    int h = slots[slot].h > 0 ? slots[slot].h : content_h;

    // Allow large windows (eg Doom 640x400), but keep them on-screen.
    if (w > ugfx_width - 2 * (GAP + BORDER))
        w = ugfx_width - 2 * (GAP + BORDER);
    if (h > ugfx_height - TASKBAR_H - 2 * (GAP + BORDER) - TITLE_BAR_H)
        h = ugfx_height - TASKBAR_H - 2 * (GAP + BORDER) - TITLE_BAR_H;
    if (w < 16)
        w = 16;
    if (h < 16)
        h = 16;

    *out_w = w;
    *out_h = h;
}

static void get_slot_frame(int slot, int *fx, int *fy, int *fw, int *fh) {
    int win_w, win_h;
    get_slot_content_size(slot, &win_w, &win_h);

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
    return (mx >= fx && mx < fx + fw && my >= fy &&
            my < fy + TITLE_BAR_H + 2 * BORDER);
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
    if (content_w > 500)
        content_w = 500;
    if (content_h > 350)
        content_h = 350;
    if (content_w < 120)
        content_w = 120;
    if (content_h < 72)
        content_h = 72;
}

static unsigned int next_place_rand(void) {
    place_seed = place_seed * 1664525u + 1013904223u;
    return place_seed;
}

static void place_slot_random(int slot, unsigned int salt) {
    int slot_w, slot_h;
    get_slot_content_size(slot, &slot_w, &slot_h);

    int min_x = GAP + BORDER;
    int max_x = ugfx_width - GAP - BORDER - slot_w;
    int min_y = TASKBAR_H + GAP + BORDER + TITLE_BAR_H;
    int max_y = ugfx_height - GAP - BORDER - slot_h;

    if (max_x < min_x)
        max_x = min_x;
    if (max_y < min_y)
        max_y = min_y;

    place_seed ^= salt + (unsigned int)(slot * 2654435761u);
    unsigned int rx = next_place_rand();
    unsigned int ry = next_place_rand();

    int span_x = (max_x - min_x) + 1;
    int span_y = (max_y - min_y) + 1;
    slots[slot].x = min_x + (int)(rx % (unsigned int)span_x);
    slots[slot].y = min_y + (int)(ry % (unsigned int)span_y);
}

static inline void bb_pixel(int x, int y, unsigned char c) {
    if (x < 0 || x >= ugfx_width || y < 0 || y >= ugfx_height)
        return;
    wm_backbuf[y * ugfx_width + x] = c;
}

static void bb_rect(int x, int y, int w, int h, unsigned char c) {
    ugfx_buf_rect(wm_backbuf, ugfx_width, ugfx_height, x, y, w, h, c);
}

static void bb_hline(int x, int y, int w, unsigned char c) {
    ugfx_buf_hline(wm_backbuf, ugfx_width, ugfx_height, x, y, w, c);
}

static void bb_vline(int x, int y, int h, unsigned char c) {
    for (int i = 0; i < h; i++)
        bb_pixel(x, y + i, c);
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

static void bb_string_fit(int x, int y, int max_w, const char *s,
                          unsigned char c) {
    if (!s || max_w <= 0)
        return;
    int max_chars = max_w / 8;
    if (max_chars <= 0)
        return;
    int len = wm_strlen(s);
    if (len <= max_chars) {
        bb_string(x, y, s, c);
        return;
    }
    if (max_chars <= 3)
        return;
    char tmp[64];
    int keep = max_chars - 3;
    if (keep > (int)sizeof(tmp) - 4)
        keep = (int)sizeof(tmp) - 4;
    int i;
    for (i = 0; i < keep && s[i]; i++)
        tmp[i] = s[i];
    tmp[i++] = '.';
    tmp[i++] = '.';
    tmp[i++] = '.';
    tmp[i] = '\0';
    bb_string(x, y, tmp, c);
}

static void bb_string_bg(int x, int y, const char *s, unsigned char fg,
                         unsigned char bg) {
    int len = wm_strlen(s);
    if (len > 0)
        bb_rect(x - 2, y - 1, len * 8 + 4, 10, bg);
    bb_string(x, y, s, fg);
}

static int hit_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

static void launch_term(void) {
    int pid = spawn("bin/winterm.wlf");
    (void)pid;
}

static void launch_files(void) {
    int pid = spawn("bin/winfm.wlf");
    (void)pid;
}

static void launch_tasks(void) {
    int pid = spawn("bin/wintask.wlf");
    (void)pid;
}

static void bb_draw_bitmap16(int x, int y, const unsigned short *rows,
                             unsigned char fg, int scale) {
    if (scale < 1)
        scale = 1;
    for (int ry = 0; ry < 16; ry++) {
        unsigned short bits = rows[ry];
        for (int rx = 0; rx < 16; rx++) {
            if (bits & (1u << (15 - rx))) {
                bb_rect(x + rx * scale, y + ry * scale, scale, scale, fg);
            }
        }
    }
}

static const unsigned short icon_term_bits[16] = {
    0x0000, 0x7FFE, 0x4002, 0x5FF2, 0x500A, 0x57C2, 0x5002, 0x53F2,
    0x5202, 0x5002, 0x5FFC, 0x4002, 0x7FFE, 0x0000, 0x0000, 0x0000};
static const unsigned short icon_folder_bits[16] = {
    0x0000, 0x0FC0, 0x1FF8, 0x3C1C, 0x3FFE, 0x7FFE, 0x6006, 0x6006,
    0x6006, 0x6006, 0x6006, 0x7FFE, 0x3FFC, 0x0000, 0x0000, 0x0000};
static const unsigned short icon_tasks_bits[16] = {
    0x0000, 0x7FFE, 0x4002, 0x5A5A, 0x5A5A, 0x4002, 0x7FFE, 0x0000,
    0x318C, 0x318C, 0x318C, 0x318C, 0x7FFE, 0x0000, 0x0000, 0x0000};

static void draw_desktop_icon(int x, int y, unsigned char body,
                              const char *label) {
    const unsigned short *glyph = icon_term_bits;
    if (label && label[0] == 'F')
        glyph = icon_folder_bits;
    if (label && label[0] == 'T' && label[1] == 'A')
        glyph = icon_tasks_bits;

    bb_rect(x + 3, y + 3, 32, 28, COL_SHADOW_NEAR);
    bb_rect(x + 1, y + 1, 32, 28, COL_SURFACE_EDGE);
    bb_rect(x, y, 32, 28, COL_SURFACE);
    bb_rect_outline(x, y, 32, 28, COL_BORDER_ACT);
    bb_rect(x + 2, y + 2, 28, 24, body);
    bb_draw_bitmap16(x + 7, y + 6, glyph, 255, 1);
    int llen = wm_strlen(label);
    int label_w = llen * 8;
    int tx = x + (32 - label_w) / 2;
    if (tx < x - 4)
        tx = x - 4;
    bb_rect(x - 4, y + 31, 44, 11, COL_DESKTOP_A);
    bb_string(tx, y + 33, label, 255);
}

static void draw_desktop_icons(void) {
    draw_desktop_icon(DS_TERM_X, DS_TERM_Y, 2, "TERM");
    draw_desktop_icon(DS_FILES_X, DS_FILES_Y, 3, "FILES");
    draw_desktop_icon(DS_TASK_X, DS_TASK_Y, 6, "TASKS");
}

static void draw_wallpaper(void) {
    bb_rect(0, 0, ugfx_width, ugfx_height, COL_DESKTOP_A);
}

static void draw_taskbar(void) {
    const char *wm_label = "mateOS WM";
    int wm_label_w = wm_strlen(wm_label) * 8;
    int wm_badge_w = wm_label_w + 12;
    if (wm_badge_w < 76)
        wm_badge_w = 76;

    bb_rect(0, 0, ugfx_width, TASKBAR_H, COL_TASKBAR_BG);
    bb_hline(0, 0, ugfx_width, COL_BORDER_ACT);
    bb_hline(0, TASKBAR_H - 2, ugfx_width, COL_TASKBAR_STRIP);
    bb_hline(0, TASKBAR_H - 1, ugfx_width, COL_BORDER_ACT);

    bb_rect(4, 3, wm_badge_w, 14, COL_TASKBAR_STRIP);
    bb_rect_outline(4, 3, wm_badge_w, 14, COL_BORDER_ACT);
    bb_string(8, 6, wm_label, COL_TASKBAR_TXT);

    if (slot_is_active(focus)) {
        int pill_x = 4 + wm_badge_w + 10;
        int pill_w = ugfx_width - pill_x - 96;
        if (pill_w > 40) {
            bb_rect(pill_x, 3, pill_w, 14, COL_SURFACE_EDGE);
            bb_rect(pill_x + 1, 4, pill_w - 2, 12, COL_SURFACE);
            bb_string_fit(pill_x + 4, 6, pill_w - 8, slots[focus].title,
                          COL_TITLE_TXT_DIM);
        }
    }

    bb_rect(ugfx_width - 92, 3, 88, 14, COL_SURFACE_EDGE);
    bb_rect(ugfx_width - 91, 4, 86, 12, COL_TASKBAR_BG);
    bb_string(ugfx_width - 84, 6, "Tab cycle", COL_TASKBAR_MUTED);
}

static void load_kversion_once(void) {
    if (g_kversion[0])
        return;
    int fd = open("/proc/kversion.mos", O_RDONLY);
    if (fd < 0) {
        wm_strcpy(g_kversion, "mateOS", sizeof(g_kversion));
        return;
    }
    int n = fd_read(fd, g_kversion, sizeof(g_kversion) - 1);
    close(fd);
    if (n <= 0) {
        wm_strcpy(g_kversion, "mateOS", sizeof(g_kversion));
        return;
    }
    g_kversion[n] = '\0';
    for (int i = 0; g_kversion[i]; i++) {
        if (g_kversion[i] == '\n' || g_kversion[i] == '\r') {
            g_kversion[i] = '\0';
            break;
        }
    }
}

static void set_info_line(int idx, const char *text) {
    if (idx < 0 || idx >= (int)(sizeof(g_info_lines) / sizeof(g_info_lines[0])))
        return;
    wm_strcpy(g_info_lines[idx], text, sizeof(g_info_lines[idx]));
}

static void build_system_info(void) {
    load_kversion_once();

    unsigned int ticks = get_ticks();
    unsigned int secs = ticks / 100u;
    unsigned int h = secs / 3600u;
    unsigned int m = (secs / 60u) % 60u;
    unsigned int s = secs % 60u;

    unsigned int ip_be = 0, mask_be = 0, gw_be = 0;
    int have_net = (net_get(&ip_be, &mask_be, &gw_be) == 0 && ip_be != 0);

    taskinfo_entry_t tasks[32];
    int tcount = tasklist(tasks, 32);
    if (tcount < 0)
        tcount = 0;

    char line[96];
    line[0] = '\0';
    wm_append_str(line, sizeof(line), g_kversion[0] ? g_kversion : "mateOS");
    set_info_line(0, line);

    line[0] = '\0';
    wm_append_str(line, sizeof(line), "UP ");
    wm_append_u32(line, sizeof(line), h);
    wm_append_str(line, sizeof(line), ":");
    wm_append_2d(line, sizeof(line), m);
    wm_append_str(line, sizeof(line), ":");
    wm_append_2d(line, sizeof(line), s);
    set_info_line(1, line);

    line[0] = '\0';
    if (have_net) {
        wm_append_str(line, sizeof(line), "IP ");
        wm_append_u32(line, sizeof(line), (ip_be >> 24) & 0xFFu);
        wm_append_str(line, sizeof(line), ".");
        wm_append_u32(line, sizeof(line), (ip_be >> 16) & 0xFFu);
        wm_append_str(line, sizeof(line), ".");
        wm_append_u32(line, sizeof(line), (ip_be >> 8) & 0xFFu);
        wm_append_str(line, sizeof(line), ".");
        wm_append_u32(line, sizeof(line), ip_be & 0xFFu);
    } else {
        wm_append_str(line, sizeof(line), "IP (not set)");
    }
    set_info_line(2, line);

    line[0] = '\0';
    wm_append_str(line, sizeof(line), "WIN ");
    wm_append_u32(line, sizeof(line), (unsigned int)num_slots);
    wm_append_str(line, sizeof(line), "  TASK ");
    wm_append_u32(line, sizeof(line), (unsigned int)tcount);
    set_info_line(3, line);

    line[0] = '\0';
    wm_append_str(line, sizeof(line), "FOCUS ");
    if (slot_is_active(focus) && slots[focus].title[0]) {
        wm_append_str(line, sizeof(line), slots[focus].title);
    } else {
        wm_append_str(line, sizeof(line), "(none)");
    }
    set_info_line(4, line);

    g_info_line_count = 5;
    g_last_info_refresh = ticks;
}

static void draw_system_info_panel(void) {
    int panel_w = 300;
    int panel_h = 10 + g_info_line_count * 10;
    int x = ugfx_width - panel_w - 8;
    int y = TASKBAR_H + 8;
    if (x < 0)
        x = 0;

    bb_rect(x + 2, y + 2, panel_w, panel_h, COL_SHADOW_NEAR);
    bb_rect(x, y, panel_w, panel_h, COL_SURFACE);
    bb_rect_outline(x, y, panel_w, panel_h, COL_BORDER_ACT);
    bb_hline(x + 1, y + 1, panel_w - 2, COL_TASKBAR_STRIP);

    for (int i = 0; i < g_info_line_count; i++) {
        bb_string_bg(x + 6, y + 4 + i * 10, g_info_lines[i],
                     (i == 0) ? COL_TASKBAR_TXT : COL_TITLE_TXT_DIM,
                     COL_SURFACE);
    }
}

static void draw_window_shadow(int fx, int fy, int fw, int fh) {
    bb_rect(fx + 2, fy + fh, fw + 2, 1, COL_SHADOW_NEAR);
    bb_rect(fx + 3, fy + fh + 1, fw + 2, 1, COL_SHADOW_FAR);
    bb_rect(fx + fw, fy + 2, 1, fh + 2, COL_SHADOW_NEAR);
    bb_rect(fx + fw + 1, fy + 3, 1, fh + 2, COL_SHADOW_FAR);
}

static void draw_window_frame(int slot, int is_focused) {
    int fx, fy, fw, fh;
    get_slot_frame(slot, &fx, &fy, &fw, &fh);
    int win_w = fw - 2 * BORDER;
    int title_col_a = is_focused ? COL_TITLE_ACT_A : COL_TITLE_INACT_A;
    int title_col_b = is_focused ? COL_TITLE_ACT_B : COL_TITLE_INACT_B;

    draw_window_shadow(fx, fy, fw, fh);

    bb_rect_outline(fx, fy, fw, fh,
                    is_focused ? COL_BORDER_ACT : COL_BORDER_INACT);
    bb_rect(fx + BORDER, fy + BORDER, win_w, TITLE_BAR_H, title_col_a);
    bb_hline(fx + BORDER, fy + BORDER + 1, win_w, title_col_b);
    bb_hline(fx + BORDER, fy + BORDER, win_w, COL_BORDER_ACT);
    bb_hline(fx + BORDER, fy + BORDER + TITLE_BAR_H - 1, win_w,
             COL_SURFACE_EDGE);
    bb_rect(fx + BORDER, fy + BORDER + TITLE_BAR_H, win_w,
            fh - TITLE_BAR_H - 2 * BORDER, COL_SURFACE);
    bb_rect_outline(fx + BORDER, fy + BORDER + TITLE_BAR_H, win_w,
                    fh - TITLE_BAR_H - 2 * BORDER, COL_SURFACE_EDGE);

    if (slot_is_active(slot)) {
        int title_x = fx + BORDER + 4;
        int close_left = fx + fw - BORDER - CLOSE_W - 2;
        int title_max_w = close_left - 4 - title_x;
        if (title_max_w < 0)
            title_max_w = 0;
        bb_string_fit(title_x, fy + BORDER + 3, title_max_w, slots[slot].title,
                      COL_TITLE_TXT);
        if (is_focused) {
            int tlen = 0;
            while (slots[slot].title[tlen])
                tlen++;
            int ulw = 10 + tlen * 4;
            if (ulw > win_w - 28)
                ulw = win_w - 28;
            if (ulw > 6)
                bb_hline(fx + BORDER + 4, fy + BORDER + TITLE_BAR_H - 3, ulw,
                         COL_TITLE_TXT_DIM);
        }

        // Close button (Windows-like titlebar X)
        int bx = fx + fw - BORDER - CLOSE_W - 2;
        int by = fy + BORDER + 1;
        int bw = CLOSE_W;
        int bh = TITLE_BAR_H - 2;
        bb_rect(bx, by, bw, bh, is_focused ? 12 : 8);
        bb_hline(bx + 1, by + 1, bw - 2,
                 is_focused ? title_col_b : COL_TITLE_INACT_B);
        bb_rect_outline(bx, by, bw, bh, 15);
        bb_string(bx + 2, by + 2, "X", 15);
    }
}

static void composite_window(int slot) {
    if (!slot_is_active(slot))
        return;

    int win_w, win_h;
    get_slot_content_size(slot, &win_w, &win_h);

    int buf_size = win_w * win_h;
    if (buf_size > (int)sizeof(read_buf))
        buf_size = (int)sizeof(read_buf);

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
        if (dy < 0 || dy >= ugfx_height)
            continue;

        for (int col = 0; col < win_w; col++) {
            int dx = sx + col;
            if (dx < 0 || dx >= ugfx_width)
                continue;

            int idx = row * win_w + col;
            unsigned char c = (idx < bytes) ? read_buf[idx] : 0;
            bb_pixel(dx, dy, c);
        }
    }
}

static void draw_cursor(int mx, int my) {
    for (int row = 0; row < CURSOR_H; row++) {
        int y = my + row;
        if (y < 0 || y >= ugfx_height)
            continue;
        unsigned char mask = cursor_mask[row];
        unsigned char bits = cursor_data[row];
        for (int col = 0; col < CURSOR_W; col++) {
            int x = mx + col;
            if (x < 0 || x >= ugfx_width)
                continue;
            if (mask & (0x80 >> col)) {
                unsigned char color =
                    (bits & (0x80 >> col)) ? COL_CURSOR : COL_SHADOW_FAR;
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
        if (!slot_is_active(s))
            continue;
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
        if (slot_is_active(s))
            num_slots++;
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
            if (hit_rect(mx, my, DS_FILES_X, DS_FILES_Y, DS_ICON_W,
                         DS_ICON_H)) {
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
    draw_wallpaper();
    draw_desktop_icons();
    draw_system_info_panel();

    // Back to front
    for (int i = 0; i < z_count; i++) {
        int s = z_order[i];
        if (!slot_is_active(s))
            continue;
        draw_window_frame(s, s == focus);
        composite_window(s);
    }

    draw_taskbar();
    draw_cursor(mx, my);

    ugfx_present(wm_backbuf, ugfx_width, ugfx_height);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

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
    build_system_info();

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

    int pid0 = spawn("bin/winterm.wlf");
    if (pid0 >= 0) {
        slots[0].pid = pid0;
        wm_strcpy(slots[0].title, "Term 1", 32);
    }

    int pid1 = spawn("bin/winfm.wlf");
    if (pid1 >= 0) {
        slots[1].pid = pid1;
        wm_strcpy(slots[1].title, "FileMgr", 32);
    }

    for (int i = 0; i < 30; i++)
        yield();
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
        if (mx < 0)
            mx = 0;
        if (my < 0)
            my = 0;
        if (mx >= ugfx_width)
            mx = ugfx_width - 1;
        if (my >= ugfx_height)
            my = ugfx_height - 1;

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
        if ((get_ticks() - g_last_info_refresh) >= 100u) {
            build_system_info();
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
