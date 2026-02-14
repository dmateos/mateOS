#include "ugfx.h"
#include "syscalls.h"
#include "libc.h"

#define W 500
#define H 350
#define MAX_FILES 256
#define NAME_MAX 32

#define TOPBAR_H 16
#define STATUS_H 14
#define PAD_X 10
#define PAD_Y 8
#define CELL_W 78
#define CELL_H 64
#define ICON_W 28
#define ICON_H 24
#define ICON_TXT_Y 34

// Palette choices tuned for a retro Windows 3.x vibe.
#define COL_BG         7
#define COL_PANEL      8
#define COL_LIGHT      15
#define COL_DARK       0
#define COL_TITLE      1
#define COL_TITLE_TXT  15
#define COL_ICON       3
#define COL_TEXT       0
#define COL_SEL_BG     9
#define COL_SEL_TXT    15
#define COL_STATUS     6

static unsigned char buf[W * H];
static char files[MAX_FILES][NAME_MAX];
static int file_count = 0;
static int file_total = 0;
static int selected = 0;
static int view_first = 0;
static int wid = -1;
static char status[80] = "Arrows/WASD Move  [/ ] Page  Enter Open  Del Delete  R Refresh  Q Quit";

static int str_ends_with(const char *s, const char *suffix) {
    int sl = strlen(s);
    int tl = strlen(suffix);
    if (sl < tl) return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

static unsigned char icon_color_for_name(const char *name) {
    if (str_ends_with(name, ".elf")) return 2;   // green: executable
    if (str_ends_with(name, ".ker")) return 14;  // yellow: kernel virtual file
    if (str_ends_with(name, ".htm")) return 11;  // cyan: web/html
    if (str_ends_with(name, ".txt")) return 10;  // light green: text
    return COL_ICON;
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

static void draw_file_icon(int x, int y, int selected_cell, const char *name) {
    if (selected_cell) {
        ugfx_buf_rect(buf, W, H, x + 2, y + 2, CELL_W - 4, CELL_H - 4, COL_SEL_BG);
    }

    int ix = x + (CELL_W - ICON_W) / 2;
    int iy = y + 6;
    ugfx_buf_rect(buf, W, H, ix, iy, ICON_W, ICON_H, icon_color_for_name(name));
    ugfx_buf_hline(buf, W, H, ix, iy, ICON_W, COL_LIGHT);
    ugfx_buf_hline(buf, W, H, ix, iy + ICON_H - 1, ICON_W, COL_DARK);
    for (int i = 0; i < ICON_H; i++) {
        ugfx_buf_pixel(buf, W, H, ix, iy + i, COL_LIGHT);
        ugfx_buf_pixel(buf, W, H, ix + ICON_W - 1, iy + i, COL_DARK);
    }
    ugfx_buf_rect(buf, W, H, ix + ICON_W - 8, iy, 8, 6, COL_LIGHT);

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
    file_count = 0;
    file_total = 0;

    char name[NAME_MAX];
    int i = 0;
    while (readdir((unsigned int)i, name, sizeof(name)) > 0) {
        if (i < MAX_FILES) {
            int j = 0;
            while (name[j] && j < NAME_MAX - 1) {
                files[i][j] = name[j];
                j++;
            }
            files[i][j] = '\0';
            file_count++;
        }
        i++;
    }
    file_total = i;

    if (selected >= file_count) selected = (file_count > 0) ? (file_count - 1) : 0;
    ensure_selected_visible();
}

static void draw_scrollbar(int cols, int rows) {
    int page = cols * rows;
    if (file_count <= page || page <= 0) return;

    int track_x = W - 10;
    int track_y = TOPBAR_H + 6;
    int track_h = H - TOPBAR_H - STATUS_H - 10;
    if (track_h < 20) return;

    ugfx_buf_rect(buf, W, H, track_x, track_y, 6, track_h, COL_PANEL);
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
}

static void redraw(void) {
    ugfx_buf_clear(buf, W, H, COL_BG);

    ugfx_buf_rect(buf, W, H, 0, 0, W, TOPBAR_H, COL_TITLE);
    ugfx_buf_string(buf, W, H, 6, 4, "Program Manager - File Manager", COL_TITLE_TXT);

    draw_bevel(4, TOPBAR_H + 2, W - 8, H - TOPBAR_H - STATUS_H - 6);

    int cols, rows, page;
    grid_dims(&cols, &rows, &page);

    int area_x = PAD_X;
    int area_y = TOPBAR_H + PAD_Y + 4;
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
    ugfx_buf_string(buf, W, H, 4, H - STATUS_H + 3, status, COL_TEXT);

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
    ugfx_buf_string(buf, W, H, W - (p * 8) - 4, H - STATUS_H + 3, info, COL_TEXT);
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
    if (str_ends_with(name, ".elf")) {
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
    if (str_ends_with(name, ".elf")) {
        copy_status("Refusing to delete .elf");
        return;
    }
    if (unlink(name) != 0) {
        copy_status("Delete failed");
        return;
    }

    copy_status("Deleted");
    load_files();
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
