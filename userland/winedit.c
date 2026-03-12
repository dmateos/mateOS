// Window text editor for mateOS WM
// Ctrl+O open  Ctrl+S save  Ctrl+N new  Ctrl+B build  ESC quit
// Arrow keys move cursor, Home/End, PgUp/PgDn, insert/delete at cursor

#include "libc.h"
#include "syscalls.h"
#include "ugfx.h"

#define W 500
#define H 350
#define MAX_TEXT 8192
#define MAX_PATH 64
#define HEADER_H 10
#define STATUS_H 10
#define TEXT_TOP  (HEADER_H + 2)
#define TEXT_BOT  (H - STATUS_H)
#define CHAR_W    8
#define CHAR_H    10
#define MARGIN    4
#define COLS      ((W - MARGIN * 2) / CHAR_W)   // chars per visual line
#define ROWS      ((TEXT_BOT - TEXT_TOP) / CHAR_H)

/* Ctrl key codes */
#define CTRL_B 2
#define CTRL_O 15
#define CTRL_S 19
#define CTRL_N 14

/* Editor modes */
#define MODE_EDIT   0
#define MODE_OPEN   1
#define MODE_SAVEAS 2

static unsigned char buf[W * H];
static char text[MAX_TEXT];
static int  text_len = 0;
static int  cursor   = 0; // byte offset into text[]
static int  scroll_row = 0; // first visible logical row

static char filepath[MAX_PATH];
static char input_buf[MAX_PATH];
static int  input_len = 0;
static int  mode = MODE_EDIT;
static char status[80];
static int  wid = -1;
static int  dirty = 0;

// ---- String helpers ----
static void set_status(const char *msg) {
    int i = 0;
    while (msg[i] && i < (int)sizeof(status) - 1) {
        status[i] = msg[i]; i++;
    }
    status[i] = '\0';
}

static int str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

// ---- Cursor movement helpers ----

// Return the logical row number of byte offset p (0-based, counts \n's)
static int offset_to_row(int p) {
    int row = 0;
    int col = 0;
    for (int i = 0; i < p && i < text_len; i++) {
        if (text[i] == '\n') { row++; col = 0; }
        else { col++; if (col >= COLS) { row++; col = 0; } }
    }
    return row;
}

// Return the column of byte offset p within its logical row
static int offset_to_col(int p) {
    // Walk back to start of logical row
    int col = 0;
    for (int i = 0; i < p && i < text_len; i++) {
        if (text[i] == '\n') col = 0;
        else { col++; if (col >= COLS) col = 0; }
    }
    return col;
}

// Return byte offset of the start of logical row `target_row`
static int row_start(int target_row) {
    int row = 0, col = 0;
    for (int i = 0; i < text_len; i++) {
        if (row == target_row) return i;
        if (text[i] == '\n') { row++; col = 0; }
        else { col++; if (col >= COLS) { row++; col = 0; } }
    }
    return text_len; // past end
}

// Total number of logical rows
static int total_rows(void) {
    return offset_to_row(text_len) + 1;
}

// Move cursor left by one character
static void cursor_left(void) {
    if (cursor > 0) cursor--;
}

// Move cursor right by one character
static void cursor_right(void) {
    if (cursor < text_len) cursor++;
}

// Move cursor up one logical row, preserving column if possible
static void cursor_up(void) {
    int row = offset_to_row(cursor);
    int col = offset_to_col(cursor);
    if (row == 0) { cursor = 0; return; }
    int prev = row_start(row - 1);
    // Advance up to col characters along the new row (stop at \n or end)
    int c = 0;
    while (c < col && prev + c < text_len && text[prev + c] != '\n')
        c++;
    cursor = prev + c;
}

// Move cursor down one logical row, preserving column if possible
static void cursor_down(void) {
    int row = offset_to_row(cursor);
    int col = offset_to_col(cursor);
    int tr  = total_rows();
    if (row >= tr - 1) { cursor = text_len; return; }
    int next = row_start(row + 1);
    int c = 0;
    while (c < col && next + c < text_len && text[next + c] != '\n')
        c++;
    cursor = next + c;
}

// Home: move to start of current logical row
static void cursor_home(void) {
    int row = offset_to_row(cursor);
    cursor = row_start(row);
}

// End: move to end of current logical row (before \n or text_len)
static void cursor_end(void) {
    // advance until \n or wrap or end
    int col = offset_to_col(cursor);
    while (cursor < text_len && text[cursor] != '\n') {
        int next_col = col + 1;
        if (next_col >= COLS) break; // wrapped — stop before wrap
        cursor++;
        col = next_col;
    }
}

// Ensure scroll_row keeps cursor visible
static void scroll_to_cursor(void) {
    int crow = offset_to_row(cursor);
    if (crow < scroll_row)
        scroll_row = crow;
    if (crow >= scroll_row + ROWS)
        scroll_row = crow - ROWS + 1;
    if (scroll_row < 0)
        scroll_row = 0;
}

// ---- Insert / delete ----
static void insert_char(char c) {
    if (text_len >= MAX_TEXT - 1) return;
    // Shift right
    for (int i = text_len; i > cursor; i--)
        text[i] = text[i - 1];
    text[cursor] = c;
    text_len++;
    cursor++;
    text[text_len] = '\0';
    dirty = 1;
}

static void delete_before_cursor(void) {
    if (cursor == 0) return;
    for (int i = cursor - 1; i < text_len - 1; i++)
        text[i] = text[i + 1];
    text_len--;
    cursor--;
    text[text_len] = '\0';
    dirty = 1;
}

// ---- Draw ----
static void redraw(void) {
    ugfx_buf_clear(buf, W, H, 15);

    // Header
    ugfx_buf_rect(buf, W, H, 0, 0, W, HEADER_H, 8);
    char title[80];
    // build_title inline to avoid the double-loop bug above
    {
        const char *prefix = "Editor";
        int i = 0;
        while (prefix[i] && i < (int)sizeof(title) - 1) { title[i] = prefix[i]; i++; }
        if (filepath[0]) {
            if (i < (int)sizeof(title)-1) title[i++] = ' ';
            if (i < (int)sizeof(title)-1) title[i++] = '-';
            if (i < (int)sizeof(title)-1) title[i++] = ' ';
            int j = 0;
            while (filepath[j] && i < (int)sizeof(title)-1) title[i++] = filepath[j++];
        }
        if (dirty && i < (int)sizeof(title)-2) { title[i++] = ' '; title[i++] = '*'; }
        title[i] = '\0';
    }
    ugfx_buf_string(buf, W, H, 4, 1, title, 15);

    // Text area: render logical rows starting from scroll_row
    int row = 0, col = 0;
    int vis_row = -1; // current visual row being rendered
    int cur_vrow = -1, cur_vcol = -1; // visual position of cursor

    // Walk text, render rows [scroll_row .. scroll_row+ROWS-1]
    int i = 0;
    // Skip rows before scroll_row
    while (i < text_len && row < scroll_row) {
        if (text[i] == '\n') { row++; col = 0; }
        else { col++; if (col >= COLS) { row++; col = 0; } }
        i++;
    }
    // Count scroll start index
    vis_row = 0;
    col = 0;

    // Track where cursor falls in visual space
    // Re-walk from beginning for cursor position (small file, acceptable)
    {
        int r2 = 0, c2 = 0;
        for (int j = 0; j <= text_len; j++) {
            if (j == cursor) {
                cur_vrow = r2 - scroll_row;
                cur_vcol = c2;
                break;
            }
            if (j < text_len) {
                if (text[j] == '\n') { r2++; c2 = 0; }
                else { c2++; if (c2 >= COLS) { r2++; c2 = 0; } }
            }
        }
    }

    // Render visible rows
    int px = MARGIN, py = TEXT_TOP;
    col = 0;
    for (; i < text_len && vis_row < ROWS; i++) {
        if (text[i] == '\n') {
            col = 0;
            vis_row++;
            px = MARGIN;
            py += CHAR_H;
        } else {
            if (col >= COLS) {
                col = 0;
                vis_row++;
                px = MARGIN;
                py += CHAR_H;
            }
            if (vis_row < ROWS) {
                ugfx_buf_char(buf, W, H, px, py, text[i], 0);
                px += CHAR_W;
                col++;
            }
        }
    }

    // Draw cursor block
    if (cur_vrow >= 0 && cur_vrow < ROWS) {
        int cx = MARGIN + cur_vcol * CHAR_W;
        int cy = TEXT_TOP + cur_vrow * CHAR_H;
        ugfx_buf_rect(buf, W, H, cx, cy, CHAR_W - 1, CHAR_H, 0);
        // Draw character under cursor in inverse if any
        if (cursor < text_len && text[cursor] != '\n') {
            ugfx_buf_char(buf, W, H, cx, cy, text[cursor], 15);
        }
    }

    // Status bar
    ugfx_buf_rect(buf, W, H, 0, H - STATUS_H, W, STATUS_H, 8);
    if (mode == MODE_OPEN) {
        ugfx_buf_string(buf, W, H, 4, H-STATUS_H+1, "Open: ", 14);
        ugfx_buf_string(buf, W, H, 4+6*CHAR_W, H-STATUS_H+1, input_buf, 15);
    } else if (mode == MODE_SAVEAS) {
        ugfx_buf_string(buf, W, H, 4, H-STATUS_H+1, "Save as: ", 14);
        ugfx_buf_string(buf, W, H, 4+9*CHAR_W, H-STATUS_H+1, input_buf, 15);
    } else {
        if (status[0])
            ugfx_buf_string(buf, W, H, 4, H-STATUS_H+1, status, 15);
        else
            ugfx_buf_string(buf, W, H, 4, H-STATUS_H+1,
                            "^O Open  ^S Save  ^B Build  ^N New  ESC Quit", 7);
    }
}

static void flush(void) {
    scroll_to_cursor();
    redraw();
    win_write(wid, buf, sizeof(buf));
}

// ---- File I/O ----
static int load_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    text_len = 0;
    while (text_len < MAX_TEXT - 1) {
        int n = fd_read(fd, text + text_len, (unsigned int)(MAX_TEXT - 1 - text_len));
        if (n <= 0) break;
        text_len += n;
    }
    text[text_len] = '\0';
    close(fd);
    return 0;
}

static int save_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    int written = 0;
    while (written < text_len) {
        int n = fd_write(fd, text + written, (unsigned int)(text_len - written));
        if (n <= 0) { close(fd); return -1; }
        written += n;
    }
    close(fd);
    return 0;
}

static void do_open(const char *path) {
    if (load_file(path) == 0) {
        str_copy(filepath, path, MAX_PATH);
        cursor = 0; scroll_row = 0;
        dirty = 0;
        set_status("Opened");
    } else {
        set_status("Open failed");
    }
}

static void do_save(void) {
    if (!filepath[0]) {
        mode = MODE_SAVEAS;
        input_buf[0] = '\0'; input_len = 0;
        return;
    }
    if (save_file(filepath) == 0) { dirty = 0; set_status("Saved"); }
    else set_status("Save failed");
}

static void do_new(void) {
    text_len = 0; text[0] = '\0';
    cursor = 0; scroll_row = 0;
    filepath[0] = '\0';
    dirty = 0;
    set_status("New file");
}

static int is_c_file(void) {
    int len = strlen(filepath);
    return (len >= 2 && filepath[len-2] == '.' && filepath[len-1] == 'c');
}

static void do_compile(void) {
    if (!filepath[0]) { set_status("Save file first"); return; }
    if (!is_c_file())  { set_status("Not a .c file");  return; }
    if (dirty && save_file(filepath) < 0) { set_status("Save failed"); return; }
    dirty = 0;
    char outname[MAX_PATH];
    int len = strlen(filepath);
    if (len >= MAX_PATH - 3) { set_status("Path too long"); return; }
    memcpy(outname, filepath, (unsigned int)(len - 1));
    outname[len-1] = 'e'; outname[len] = 'l';
    outname[len+1] = 'f'; outname[len+2] = '\0';
    const char *args[] = {"bin/tcc.elf", filepath, "-o", outname};
    int child = spawn_argv(args[0], args, 4);
    if (child < 0) { set_status("tcc not found"); return; }
    int code = wait(child);
    if (code == 0) {
        set_status("Compiled OK: ");
        int slen = strlen(status);
        int i = 0;
        while (outname[i] && slen < (int)sizeof(status)-1)
            status[slen++] = outname[i++];
        status[slen] = '\0';
    } else {
        set_status("Compile failed");
    }
}

// ---- Prompt input (open / save-as) ----
static int handle_prompt_key(int key) {
    if (key == 27) {
        mode = MODE_EDIT; set_status(""); return 1;
    }
    if (key == '\n') {
        if (input_len > 0) {
            if (mode == MODE_OPEN) {
                do_open(input_buf);
            } else {
                str_copy(filepath, input_buf, MAX_PATH);
                if (save_file(filepath) == 0) { dirty = 0; set_status("Saved"); }
                else set_status("Save failed");
            }
        }
        mode = MODE_EDIT; return 1;
    }
    if (key == '\b') {
        if (input_len > 0) { input_len--; input_buf[input_len] = '\0'; }
        return 1;
    }
    if (key >= 32 && key < 127 && input_len < MAX_PATH - 1) {
        input_buf[input_len++] = (char)key;
        input_buf[input_len]   = '\0';
        return 1;
    }
    return 0;
}

void _start(int argc, char **argv) {
    filepath[0] = '\0'; status[0] = '\0'; text[0] = '\0';
    cursor = 0; scroll_row = 0;

    if (argc >= 2 && argv[1])
        do_open(argv[1]);

    char win_title[40];
    {
        const char *p = "Editor"; int i = 0;
        while (p[i] && i < (int)sizeof(win_title)-1) { win_title[i]=p[i]; i++; }
        win_title[i] = '\0';
    }
    wid = win_create(W, H, win_title);
    if (wid < 0) { print("error: requires window manager\n"); exit(1); }
    detach();

    flush();

    while (1) {
        int key = win_getkey(wid);
        if (key <= 0) { yield(); continue; }

        if (mode != MODE_EDIT) {
            if (handle_prompt_key(key)) flush();
            else yield();
            continue;
        }

        // Normal edit mode
        int moved = 0;

        if (key == 27) {
            break;
        } else if (key == CTRL_O) {
            mode = MODE_OPEN; input_buf[0] = '\0'; input_len = 0;
            flush();
        } else if (key == CTRL_S) {
            do_save(); flush();
        } else if (key == CTRL_N) {
            do_new(); flush();
        } else if (key == CTRL_B) {
            do_compile(); flush();
        } else if (key == KEY_LEFT) {
            cursor_left(); moved = 1;
        } else if (key == KEY_RIGHT) {
            cursor_right(); moved = 1;
        } else if (key == KEY_UP) {
            cursor_up(); moved = 1;
        } else if (key == KEY_DOWN) {
            cursor_down(); moved = 1;
        } else if (key == KEY_HOME || key == 1 /* Ctrl+A */) {
            cursor_home(); moved = 1;
        } else if (key == KEY_END || key == 5 /* Ctrl+E */) {
            cursor_end(); moved = 1;
        } else if (key == 21 /* Ctrl+U: page up */) {
            for (int i = 0; i < ROWS; i++) cursor_up();
            moved = 1;
        } else if (key == 4 /* Ctrl+D: page down */) {
            for (int i = 0; i < ROWS; i++) cursor_down();
            moved = 1;
        } else if (key == '\b') {
            delete_before_cursor(); flush();
        } else if (key == '\n') {
            insert_char('\n'); flush();
        } else if (key >= 32 && key < 127) {
            insert_char((char)key); flush();
        }

        if (moved) {
            status[0] = '\0'; // clear status on navigation
            flush();
        }

        yield();
    }

    win_destroy(wid);
    exit(0);
}
