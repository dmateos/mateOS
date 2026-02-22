// Window text editor - simple text editor for mateOS WM
// Supports opening files from argv or Ctrl+O, saving with Ctrl+S

#include "ugfx.h"
#include "syscalls.h"
#include "libc.h"

#define W 500
#define H 350
#define MAX_TEXT 4096
#define MAX_PATH 64
#define HEADER_H 10
#define STATUS_H 10
#define TEXT_TOP (HEADER_H + 2)
#define TEXT_BOT (H - STATUS_H)

/* Ctrl key codes (ASCII 1-26) */
#define CTRL_B 2
#define CTRL_O 15
#define CTRL_S 19
#define CTRL_N 14

/* Editor modes */
#define MODE_EDIT   0
#define MODE_OPEN   1  /* typing filename to open */
#define MODE_SAVEAS 2  /* typing filename for save-as */

static unsigned char buf[W * H];
static char text[MAX_TEXT];
static int text_len = 0;
static char filepath[MAX_PATH];  /* current file path, empty = untitled */
static char input_buf[MAX_PATH]; /* prompt input buffer for open/save-as */
static int input_len = 0;
static int mode = MODE_EDIT;
static char status[80];
static int wid = -1;
static int dirty = 0;           /* unsaved changes flag */

static void set_status(const char *msg) {
    int i = 0;
    while (msg[i] && i < (int)sizeof(status) - 1) {
        status[i] = msg[i];
        i++;
    }
    status[i] = '\0';
}

/* Copy src into dst, return length copied */
static int str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static void build_title(char *out, int max) {
    const char *prefix = "Editor";
    int i = 0;
    while (prefix[i] && i < max - 1) { out[i] = prefix[i]; i++; }
    if (filepath[0]) {
        if (i < max - 1) out[i++] = ' ';
        if (i < max - 1) out[i++] = '-';
        if (i < max - 1) out[i++] = ' ';
        int j = 0;
        while (filepath[j] && i < max - 1) { out[i++] = filepath[j++]; }
    }
    if (dirty) {
        if (i < max - 1) out[i++] = ' ';
        if (i < max - 1) out[i++] = '*';
    }
    out[i] = '\0';
}

static void redraw(void) {
    /* White background */
    ugfx_buf_clear(buf, W, H, 15);

    /* Header bar with filename */
    ugfx_buf_rect(buf, W, H, 0, 0, W, HEADER_H, 8);
    char title[80];
    build_title(title, sizeof(title));
    ugfx_buf_string(buf, W, H, 4, 1, title, 15);

    /* Draw text with wrapping */
    int x = 4, y = TEXT_TOP;
    for (int i = 0; i < text_len; i++) {
        if (text[i] == '\n' || x + 8 > W - 4) {
            x = 4;
            y += 10;
            if (y + 8 > TEXT_BOT) break;
            if (text[i] == '\n') continue;
        }
        ugfx_buf_char(buf, W, H, x, y, text[i], 0);
        x += 8;
    }

    /* Cursor */
    if (y + 8 <= TEXT_BOT) {
        ugfx_buf_rect(buf, W, H, x, y, 7, 8, 0);
    }

    /* Status bar */
    ugfx_buf_rect(buf, W, H, 0, H - STATUS_H, W, STATUS_H, 8);
    if (mode == MODE_OPEN) {
        ugfx_buf_string(buf, W, H, 4, H - STATUS_H + 1, "Open: ", 14);
        ugfx_buf_string(buf, W, H, 4 + 6 * 8, H - STATUS_H + 1, input_buf, 15);
    } else if (mode == MODE_SAVEAS) {
        ugfx_buf_string(buf, W, H, 4, H - STATUS_H + 1, "Save as: ", 14);
        ugfx_buf_string(buf, W, H, 4 + 9 * 8, H - STATUS_H + 1, input_buf, 15);
    } else {
        if (status[0]) {
            ugfx_buf_string(buf, W, H, 4, H - STATUS_H + 1, status, 15);
        } else {
            ugfx_buf_string(buf, W, H, 4, H - STATUS_H + 1,
                            "^O Open  ^S Save  ^B Build  ^N New  ESC Quit", 7);
        }
    }
}

static void flush(void) {
    redraw();
    win_write(wid, buf, sizeof(buf));
}

/* Load file contents into the text buffer */
static int load_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    text_len = 0;
    while (text_len < MAX_TEXT - 1) {
        int n = fd_read(fd, text + text_len,
                      (unsigned int)(MAX_TEXT - 1 - text_len));
        if (n <= 0) break;
        text_len += n;
    }
    text[text_len] = '\0';
    close(fd);
    return 0;
}

/* Save text buffer to file */
static int save_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    int written = 0;
    while (written < text_len) {
        int n = fd_write(fd, text + written,
                       (unsigned int)(text_len - written));
        if (n <= 0) { close(fd); return -1; }
        written += n;
    }
    close(fd);
    return 0;
}

static void do_open(const char *path) {
    if (load_file(path) == 0) {
        str_copy(filepath, path, MAX_PATH);
        dirty = 0;
        set_status("Opened");
    } else {
        set_status("Open failed");
    }
}

static void do_save(void) {
    if (!filepath[0]) {
        /* No filename yet — enter save-as mode */
        mode = MODE_SAVEAS;
        input_buf[0] = '\0';
        input_len = 0;
        return;
    }
    if (save_file(filepath) == 0) {
        dirty = 0;
        set_status("Saved");
    } else {
        set_status("Save failed");
    }
}

static void do_new(void) {
    text_len = 0;
    text[0] = '\0';
    filepath[0] = '\0';
    dirty = 0;
    set_status("New file");
}

/* Check if filepath ends with ".c" */
static int is_c_file(void) {
    int len = 0;
    while (filepath[len]) len++;
    return (len >= 2 && filepath[len - 2] == '.' && filepath[len - 1] == 'c');
}

/* Compile current .c file with TCC. Saves first, then spawns tcc. */
static void do_compile(void) {
    if (!filepath[0]) {
        set_status("Save file first");
        return;
    }
    if (!is_c_file()) {
        set_status("Not a .c file");
        return;
    }
    // Save before compiling
    if (dirty) {
        if (save_file(filepath) < 0) {
            set_status("Save failed");
            return;
        }
        dirty = 0;
    }
    // Build output name: replace .c with .elf
    char outname[MAX_PATH];
    int len = 0;
    while (filepath[len]) len++;
    if (len >= MAX_PATH - 3) {
        set_status("Path too long");
        return;
    }
    memcpy(outname, filepath, (unsigned int)(len - 1));  // copy up to '.'
    outname[len - 1] = 'e';
    outname[len]     = 'l';
    outname[len + 1] = 'f';
    outname[len + 2] = '\0';

    const char *args[] = { "tcc.elf", filepath, "-o", outname };
    int child = spawn_argv(args[0], args, 4);
    if (child < 0) {
        set_status("tcc not found");
        return;
    }
    // Wait for compilation
    int code = wait(child);
    if (code == 0) {
        set_status("Compiled: ");
        // Append output name to status
        int slen = 0;
        while (status[slen]) slen++;
        int i = 0;
        while (outname[i] && slen < (int)sizeof(status) - 1)
            status[slen++] = outname[i++];
        status[slen] = '\0';
    } else {
        set_status("Compile failed");
    }
}

/* Handle a key in prompt mode (open/save-as). Returns 1 if prompt is done. */
static int handle_prompt_key(int key) {
    if (key == 27) {
        /* ESC cancels prompt */
        mode = MODE_EDIT;
        set_status("");
        return 1;
    }
    if (key == '\n') {
        if (input_len > 0) {
            if (mode == MODE_OPEN) {
                do_open(input_buf);
            } else if (mode == MODE_SAVEAS) {
                str_copy(filepath, input_buf, MAX_PATH);
                if (save_file(filepath) == 0) {
                    dirty = 0;
                    set_status("Saved");
                } else {
                    set_status("Save failed");
                }
            }
        }
        mode = MODE_EDIT;
        return 1;
    }
    if (key == '\b') {
        if (input_len > 0) {
            input_len--;
            input_buf[input_len] = '\0';
        }
        return 1;
    }
    if (key >= 32 && key < 127 && input_len < MAX_PATH - 1) {
        input_buf[input_len++] = (char)key;
        input_buf[input_len] = '\0';
        return 1;
    }
    return 0;
}

void _start(int argc, char **argv) {
    filepath[0] = '\0';
    status[0] = '\0';
    text[0] = '\0';

    /* Open file from command line argument if provided */
    if (argc >= 2 && argv[1]) {
        do_open(argv[1]);
    }

    char win_title[40];
    build_title(win_title, sizeof(win_title));
    wid = win_create(W, H, win_title);
    if (wid < 0) {
        print("error: requires window manager\n");
        exit(1);
    }
    detach();

    flush();

    while (1) {
        int key = win_getkey(wid);
        if (key > 0) {
            if (mode != MODE_EDIT) {
                /* In prompt mode */
                if (handle_prompt_key(key)) {
                    flush();
                }
            } else {
                /* Normal edit mode */
                int changed = 0;

                if (key == 27) {
                    break;  /* ESC to quit */
                } else if (key == CTRL_O) {
                    mode = MODE_OPEN;
                    input_buf[0] = '\0';
                    input_len = 0;
                    flush();
                } else if (key == CTRL_S) {
                    do_save();
                    flush();
                } else if (key == CTRL_N) {
                    do_new();
                    flush();
                } else if (key == CTRL_B) {
                    do_compile();
                    flush();
                } else if (key == '\b') {
                    if (text_len > 0) {
                        text_len--;
                        text[text_len] = '\0';
                        changed = 1;
                        dirty = 1;
                    }
                } else if (key == '\n') {
                    if (text_len < MAX_TEXT - 1) {
                        text[text_len++] = '\n';
                        text[text_len] = '\0';
                        changed = 1;
                        dirty = 1;
                    }
                } else if (key >= 32 && key < 127) {
                    if (text_len < MAX_TEXT - 1) {
                        text[text_len++] = (char)key;
                        text[text_len] = '\0';
                        changed = 1;
                        dirty = 1;
                    }
                }

                if (changed) {
                    flush();
                }
            }
        }
        yield();
    }

    win_destroy(wid);
    exit(0);
}
