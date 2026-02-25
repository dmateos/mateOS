#include "ugfx.h"
#include "syscalls.h"
#include "libc.h"

#define W 500
#define H 350
#define MAX_FILES 256
#define NAME_MAX 32
#define MAX_EXTS 32
#define EXT_MAX 16

#define TOPBAR_H 16
#define STATUS_H 14
#define PAD_X 10
#define PAD_Y 8
#define CELL_W 78
#define CELL_H 64
#define ICON_W 28
#define ICON_H 24
#define ICON_TXT_Y 34

// 16-bit desktop path still uses indexed colors in window buffers; choose richer palette entries.
#define COL_BG         237
#define COL_PANEL      239
#define COL_PANEL_ALT  242
#define COL_LIGHT      254
#define COL_DARK       233
#define COL_TITLE      75
#define COL_TITLE_BAR2 117
#define COL_TITLE_TXT  255
#define COL_ICON       81
#define COL_TEXT       252
#define COL_MUTED      247
#define COL_SEL_BG     31
#define COL_SEL_TXT    255
#define COL_STATUS     236
#define COL_STATUS_TXT 250

static unsigned char buf[W * H];
static char files[MAX_FILES][NAME_MAX];
static char all_files[MAX_FILES][NAME_MAX];
static char ext_filters[MAX_EXTS][EXT_MAX];
static int file_count = 0;
static int all_count = 0;
static int file_total = 0;
static int ext_filter_count = 0;
static int ext_filter_idx = 0;  // 0=all, 1..N from ext_filters
static int selected = 0;
static int view_first = 0;
static int wid = -1;
static char status[80] = "Arrows/WASD Move  [/ ] Page  Enter Open  Del Delete  F Filter  R Refresh  Q Quit";

static void ensure_selected_visible(void);

static int str_ends_with(const char *s, const char *suffix) {
    int sl = strlen(s);
    int tl = strlen(suffix);
    if (sl < tl) return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

static unsigned char icon_color_for_name(const char *name) {
    if (str_ends_with(name, ".elf")) return 2;   // green: executable
    if (str_ends_with(name, ".wlf")) return 13;  // magenta: window executable
    if (str_ends_with(name, ".mos")) return 14;  // yellow: virtual OS file
    if (str_ends_with(name, ".htm")) return 11;  // cyan: web/html
    if (str_ends_with(name, ".txt")) return 10;  // light green: text
    return COL_ICON;
}

static void copy_name(char *dst, const char *src, int cap) {
    int i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char tolower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static void file_ext_token(const char *name, char *out, int cap) {
    int last_dot = -1;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') last_dot = i;
    }
    if (last_dot < 0 || !name[last_dot + 1]) {
        copy_name(out, "<none>", cap);
        return;
    }
    int j = 0;
    for (int i = last_dot + 1; name[i] && j < cap - 1; i++) {
        out[j++] = tolower_ascii(name[i]);
    }
    out[j] = '\0';
}

static int ext_exists(const char *tok) {
    for (int i = 0; i < ext_filter_count; i++) {
        if (strcmp(ext_filters[i], tok) == 0) return 1;
    }
    return 0;
}

static void sort_ext_filters(void) {
    for (int i = 0; i < ext_filter_count; i++) {
        int best = i;
        for (int j = i + 1; j < ext_filter_count; j++) {
            if (strcmp(ext_filters[j], ext_filters[best]) < 0) best = j;
        }
        if (best != i) {
            char tmp[EXT_MAX];
            copy_name(tmp, ext_filters[i], EXT_MAX);
            copy_name(ext_filters[i], ext_filters[best], EXT_MAX);
            copy_name(ext_filters[best], tmp, EXT_MAX);
        }
    }
}

static void rebuild_ext_filters(void) {
    ext_filter_count = 0;
    for (int i = 0; i < all_count && ext_filter_count < MAX_EXTS; i++) {
        char tok[EXT_MAX];
        file_ext_token(all_files[i], tok, sizeof(tok));
        if (!ext_exists(tok)) {
            copy_name(ext_filters[ext_filter_count], tok, EXT_MAX);
            ext_filter_count++;
        }
    }
    sort_ext_filters();
    if (ext_filter_idx > ext_filter_count) ext_filter_idx = 0;
}

static int filter_match(const char *name) {
    if (ext_filter_idx == 0) return 1;
    char tok[EXT_MAX];
    file_ext_token(name, tok, sizeof(tok));
    return strcmp(tok, ext_filters[ext_filter_idx - 1]) == 0;
}

static void rebuild_visible_files(void) {
    file_count = 0;
    for (int i = 0; i < all_count && file_count < MAX_FILES; i++) {
        if (!filter_match(all_files[i])) continue;
        copy_name(files[file_count], all_files[i], NAME_MAX);
        file_count++;
    }
    if (selected >= file_count) selected = (file_count > 0) ? (file_count - 1) : 0;
    if (selected < 0) selected = 0;
    ensure_selected_visible();
}

static void copy_status(const char *s) {
    int i = 0;
    while (s[i] && i < (int)sizeof(status) - 1) {
        status[i] = s[i];
        i++;
    }
    status[i] = '\0';
}

static void draw_bevel(int x, int y, int w, int h) {
    ugfx_buf_rect(buf, W, H, x, y, w, h, COL_PANEL);
    ugfx_buf_hline(buf, W, H, x, y, w, COL_LIGHT);
    ugfx_buf_hline(buf, W, H, x, y + h - 1, w, COL_DARK);
    for (int i = 0; i < h; i++) {
        ugfx_buf_pixel(buf, W, H, x, y + i, COL_LIGHT);
        ugfx_buf_pixel(buf, W, H, x + w - 1, y + i, COL_DARK);
    }
}

static void buf_vline(int x, int y, int h, unsigned char c) {
    for (int i = 0; i < h; i++) ugfx_buf_pixel(buf, W, H, x, y + i, c);
}

static void draw_bitmap16(int x, int y, const unsigned short *rows, unsigned char fg) {
    for (int ry = 0; ry < 16; ry++) {
        unsigned short bits = rows[ry];
        for (int rx = 0; rx < 16; rx++) {
            if (bits & (1u << (15 - rx))) {
                ugfx_buf_pixel(buf, W, H, x + rx, y + ry, fg);
            }
        }
    }
}

static const unsigned short glyph_file[16] = {
    0x0FF0, 0x1FF8, 0x3C1C, 0x380C, 0x300C, 0x3FFC, 0x300C, 0x3FFC,
    0x300C, 0x3FFC, 0x300C, 0x300C, 0x3FFC, 0x0000, 0x0000, 0x0000
};
static const unsigned short glyph_folder[16] = {
    0x07E0, 0x0FF8, 0x1C1C, 0x1FFE, 0x3FFE, 0x3006, 0x3006, 0x3006,
    0x3006, 0x3006, 0x3006, 0x3FFE, 0x1FFC, 0x0000, 0x0000, 0x0000
};
static const unsigned short glyph_exec[16] = {
    0x7FFE, 0x4002, 0x5FFA, 0x5A1A, 0x5A1A, 0x5FFA, 0x4002, 0x7FFE,
    0x0810, 0x0C30, 0x0E70, 0x0C30, 0x0810, 0x0000, 0x0000, 0x0000
};
static const unsigned short glyph_graph[16] = {
    0x7FFE, 0x4002, 0x5FF2, 0x500A, 0x57C2, 0x5002, 0x53F2, 0x5202,
    0x5002, 0x5FFC, 0x4002, 0x7FFE, 0x0000, 0x0000, 0x0000, 0x0000
};
static const unsigned short glyph_chip[16] = {
    0x0810, 0x1FF8, 0x3FFC, 0x2424, 0x67E6, 0x67E6, 0x67E6, 0x67E6,
    0x67E6, 0x67E6, 0x2424, 0x3FFC, 0x1FF8, 0x0810, 0x0000, 0x0000
};

static const unsigned short *icon_bitmap_for_name(const char *name) {
    if (str_ends_with(name, ".elf")) return glyph_exec;
    if (str_ends_with(name, ".wlf")) return glyph_graph;
    if (str_ends_with(name, ".mos")) return glyph_chip;
    return glyph_file;
}

static void draw_file_icon(int x, int y, int selected_cell, const char *name) {
    if (selected_cell) {
        ugfx_buf_rect(buf, W, H, x + 2, y + 2, CELL_W - 4, CELL_H - 4, COL_SEL_BG);
        ugfx_buf_hline(buf, W, H, x + 2, y + 2, CELL_W - 4, COL_TITLE_BAR2);
        ugfx_buf_hline(buf, W, H, x + 2, y + CELL_H - 3, CELL_W - 4, 24);
    } else {
        ugfx_buf_rect(buf, W, H, x + 2, y + 2, CELL_W - 4, CELL_H - 4, COL_PANEL);
    }

    int ix = x + (CELL_W - 24) / 2;
    int iy = y + 7;
    unsigned char icon_fill = icon_color_for_name(name);
    ugfx_buf_rect(buf, W, H, ix + 1, iy + 1, 24, 18, selected_cell ? 233 : COL_DARK);
    ugfx_buf_rect(buf, W, H, ix, iy, 24, 18, icon_fill);
    ugfx_buf_rect(buf, W, H, ix + 1, iy + 1, 22, 16, selected_cell ? COL_SEL_BG : COL_PANEL_ALT);
    ugfx_buf_hline(buf, W, H, ix, iy, 24, COL_LIGHT);
    ugfx_buf_hline(buf, W, H, ix, iy + 17, 24, COL_DARK);
    buf_vline(ix, iy, 18, COL_LIGHT);
    buf_vline(ix + 23, iy, 18, COL_DARK);
    if (str_ends_with(name, ".dir") || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        draw_bitmap16(ix + 4, iy + 1, glyph_folder, icon_fill);
    } else {
        draw_bitmap16(ix + 4, iy + 1, icon_bitmap_for_name(name), icon_fill);
    }

    char shortn[11];
    int nlen = strlen(name);
    int copy = (nlen <= 10) ? nlen : 10;
    for (int i = 0; i < copy; i++) shortn[i] = name[i];
    shortn[copy] = '\0';

    unsigned char tc = selected_cell ? COL_SEL_TXT : COL_TEXT;
    int tx = x + (CELL_W - (copy * 8)) / 2;
    if (tx < x + 2) tx = x + 2;
    ugfx_buf_string(buf, W, H, tx, y + ICON_TXT_Y, shortn, tc);
}

static void grid_dims(int *out_cols, int *out_rows, int *out_page) {
    int cols = (W - PAD_X * 2) / CELL_W;
    int rows = (H - TOPBAR_H - STATUS_H - PAD_Y - 10) / CELL_H;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (out_cols) *out_cols = cols;
    if (out_rows) *out_rows = rows;
    if (out_page) *out_page = cols * rows;
}

static void ensure_selected_visible(void) {
    int cols, rows, page;
    grid_dims(&cols, &rows, &page);

    if (selected < 0) selected = 0;
    if (selected >= file_count) selected = (file_count > 0) ? (file_count - 1) : 0;
    if (view_first < 0) view_first = 0;
    if (view_first > selected) view_first = selected;

    int rel = selected - view_first;
    if (rel < 0) {
        view_first = selected;
    } else if (rel >= page) {
        view_first = selected - page + 1;
    }

    if (file_count <= page) {
        view_first = 0;
    } else {
        int max_first = file_count - page;
        if (view_first > max_first) view_first = max_first;
    }
}

static void load_files(void) {
    all_count = 0;
    file_total = 0;

    char name[NAME_MAX];
    int i = 0;
    while (readdir((unsigned int)i, name, sizeof(name)) > 0) {
        if (all_count < MAX_FILES) {
            copy_name(all_files[all_count], name, NAME_MAX);
            all_count++;
        }
        i++;
    }
    file_total = i;

    rebuild_ext_filters();
    rebuild_visible_files();
}

static void draw_scrollbar(int cols, int rows) {
    int page = cols * rows;
    if (file_count <= page || page <= 0) return;

    int track_x = W - 10;
    int track_y = TOPBAR_H + 6;
    int track_h = H - TOPBAR_H - STATUS_H - 10;
    if (track_h < 20) return;

    ugfx_buf_rect(buf, W, H, track_x, track_y, 6, track_h, COL_PANEL_ALT);
    ugfx_buf_hline(buf, W, H, track_x, track_y, 6, COL_LIGHT);
    ugfx_buf_hline(buf, W, H, track_x, track_y + track_h - 1, 6, COL_DARK);

    int thumb_h = (track_h * page) / file_count;
    if (thumb_h < 8) thumb_h = 8;
    int max_first = file_count - page;
    int thumb_y = track_y;
    if (max_first > 0) {
        thumb_y = track_y + ((track_h - thumb_h) * view_first) / max_first;
    }
    ugfx_buf_rect(buf, W, H, track_x + 1, thumb_y, 4, thumb_h, COL_SEL_BG);
    ugfx_buf_hline(buf, W, H, track_x + 1, thumb_y, 4, COL_TITLE_BAR2);
}

static void redraw(void) {
    ugfx_buf_clear(buf, W, H, COL_BG);

    ugfx_buf_rect(buf, W, H, 0, 0, W, TOPBAR_H, COL_TITLE);
    ugfx_buf_hline(buf, W, H, 0, 1, W, COL_TITLE_BAR2);
    ugfx_buf_string(buf, W, H, 6, 4, "Program Manager - File Manager", COL_TITLE_TXT);

    draw_bevel(4, TOPBAR_H + 2, W - 8, H - TOPBAR_H - STATUS_H - 6);

    int cols, rows, page;
    grid_dims(&cols, &rows, &page);

    int area_x = PAD_X;
    int area_y = TOPBAR_H + PAD_Y + 2;
    int shown = 0;
    int max_i = view_first + page;
    if (max_i > file_count) max_i = file_count;

    for (int i = view_first; i < max_i; i++) {
        int local = i - view_first;
        int row = local / cols;
        int col = local % cols;
        int x = area_x + col * CELL_W;
        int y = area_y + row * CELL_H;
        draw_file_icon(x, y, i == selected, files[i]);
        shown++;
    }

    draw_scrollbar(cols, rows);

    ugfx_buf_rect(buf, W, H, 0, H - STATUS_H, W, STATUS_H, COL_STATUS);
    ugfx_buf_hline(buf, W, H, 0, H - STATUS_H, W, COL_PANEL_ALT);
    ugfx_buf_string(buf, W, H, 4, H - STATUS_H + 3, status, COL_STATUS_TXT);

    char info[32];
    int p = 0;
    int start = (file_count > 0) ? (view_first + 1) : 0;
    int end = view_first + shown;
    info[p++] = '[';
    itoa(start, info + p); while (info[p]) p++;
    info[p++] = '-';
    itoa(end, info + p); while (info[p]) p++;
    info[p++] = '/';
    itoa(file_total, info + p); while (info[p]) p++;
    if (file_total > file_count) {
        info[p++] = ' ';
        info[p++] = 'T';
        info[p++] = 'R';
        info[p++] = 'U';
        info[p++] = 'N';
        info[p++] = 'C';
    }
    info[p++] = ']';
    info[p] = '\0';
    ugfx_buf_string(buf, W, H, W - (p * 8) - 4, H - STATUS_H + 3, info, COL_MUTED);
}

static void move_selection(int delta) {
    if (file_count <= 0) return;
    selected += delta;
    if (selected < 0) selected = 0;
    if (selected >= file_count) selected = file_count - 1;
    ensure_selected_visible();
}

static void page_selection(int delta_pages) {
    int cols, rows, page;
    grid_dims(&cols, &rows, &page);
    move_selection(delta_pages * page);
}

static void spawn_for_entry(const char *name) {
    if (str_ends_with(name, ".elf") || str_ends_with(name, ".wlf")) {
        int pid = spawn(name);
        if (pid < 0) {
            copy_status("Open failed");
            return;
        }
        copy_status("Program launched");
        return;
    }

    const char *argv[3];
    argv[0] = "cat.elf";
    argv[1] = name;
    argv[2] = 0;
    int pid = spawn_argv("cat.elf", argv, 2);
    if (pid < 0) {
        copy_status("cat spawn failed");
        return;
    }
    copy_status("Viewing file via cat");
}

static void delete_selected(void) {
    if (file_count <= 0) return;

    const char *name = files[selected];
    if (str_ends_with(name, ".elf") || str_ends_with(name, ".wlf")) {
        copy_status("Refusing to delete executable");
        return;
    }
    if (unlink(name) != 0) {
        copy_status("Delete failed");
        return;
    }

    copy_status("Deleted");
    load_files();
}

static void cycle_filter(void) {
    if (ext_filter_count <= 0) {
        ext_filter_idx = 0;
        copy_status("Filter: all");
        rebuild_visible_files();
        return;
    }
    ext_filter_idx++;
    if (ext_filter_idx > ext_filter_count) ext_filter_idx = 0;

    char msg[80];
    if (ext_filter_idx == 0) {
        copy_name(msg, "Filter: all", sizeof(msg));
    } else {
        int p = 0;
        const char *pre = "Filter: .";
        while (pre[p]) { msg[p] = pre[p]; p++; }
        int j = 0;
        const char *ext = ext_filters[ext_filter_idx - 1];
        while (ext[j] && p < (int)sizeof(msg) - 1) msg[p++] = ext[j++];
        msg[p] = '\0';
    }
    copy_status(msg);
    rebuild_visible_files();
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    wid = win_create(W, H, "File Manager");
    if (wid < 0) {
        print("error: requires window manager\n");
        exit(1);
    }
    detach();

    load_files();
    redraw();
    win_write(wid, buf, sizeof(buf));

    while (1) {
        int k = win_getkey(wid);
        if (k <= 0) {
            yield();
            continue;
        }

        if (k == 'q' || k == 27) break;
        if (k == 'r' || k == 'R') {
            load_files();
            copy_status("Refreshed");
        } else if (k == 'f' || k == 'F') {
            cycle_filter();
        } else if (k == '\n' || k == '\r') {
            if (file_count > 0) spawn_for_entry(files[selected]);
        } else if (k == 127 || k == '\b') {
            delete_selected();
        } else if (k == '[') {
            page_selection(-1);
        } else if (k == ']') {
            page_selection(1);
        } else {
            int cols, rows, page;
            grid_dims(&cols, &rows, &page);
            if (k == 'a' || k == 'A' || k == KEY_LEFT) move_selection(-1);
            if (k == 'd' || k == 'D' || k == KEY_RIGHT) move_selection(1);
            if (k == 'w' || k == 'W' || k == KEY_UP) move_selection(-cols);
            if (k == 's' || k == 'S' || k == KEY_DOWN) move_selection(cols);
        }

        redraw();
        win_write(wid, buf, sizeof(buf));
    }

    win_destroy(wid);
    exit(0);
}
