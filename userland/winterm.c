// Window terminal - shell running inside a WM window
// Renders text to pixel buffer, routes I/O through window syscalls

#include "ugfx.h"
#include "syscalls.h"

#define W 500
#define H 350

// Text grid: 8x8 font with margins
#define MARGIN_X 4
#define MARGIN_Y 4
#define CHAR_W 8
#define CHAR_H 10  // 8px + 2px line spacing
#define TERM_COLS ((W - MARGIN_X * 2) / CHAR_W)   // 61
#define TERM_ROWS ((H - MARGIN_Y * 2) / CHAR_H)   // 34

static unsigned char pixbuf[W * H];
static char screen[TERM_ROWS][TERM_COLS + 1];  // +1 for safety
static int cur_row = 0, cur_col = 0;
static int wid = -1;

// ---- String helpers ----

static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int my_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

// ---- Terminal rendering ----

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            screen[r][c] = screen[r + 1][c];
        }
    }
    for (int c = 0; c < TERM_COLS; c++) {
        screen[TERM_ROWS - 1][c] = ' ';
    }
    cur_row = TERM_ROWS - 1;
}

static void term_putchar(char ch) {
    if (ch == '\n') {
        cur_col = 0;
        cur_row++;
        if (cur_row >= TERM_ROWS) {
            term_scroll();
        }
        return;
    }
    if (ch == '\b') {
        if (cur_col > 0) {
            cur_col--;
            screen[cur_row][cur_col] = ' ';
        }
        return;
    }
    if (ch < 32 || ch > 126) return;

    if (cur_col >= TERM_COLS) {
        cur_col = 0;
        cur_row++;
        if (cur_row >= TERM_ROWS) {
            term_scroll();
        }
    }

    screen[cur_row][cur_col] = ch;
    cur_col++;
}

static void term_print(const char *s) {
    while (*s) {
        term_putchar(*s++);
    }
}

static void term_print_num(int n) {
    if (n < 0) {
        term_putchar('-');
        n = -n;
    }
    if (n == 0) {
        term_putchar('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        term_putchar(buf[--i]);
    }
}

static void term_redraw(void) {
    // Black background
    ugfx_buf_clear(pixbuf, W, H, 0);

    // Render text
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = screen[r][c];
            if (ch > 32 && ch < 127) {
                int px = MARGIN_X + c * CHAR_W;
                int py = MARGIN_Y + r * CHAR_H;
                ugfx_buf_char(pixbuf, W, H, px, py, ch, 10);  // Light green
            }
        }
    }

    // Draw cursor (block)
    if (cur_row < TERM_ROWS) {
        int cx = MARGIN_X + cur_col * CHAR_W;
        int cy = MARGIN_Y + cur_row * CHAR_H;
        ugfx_buf_rect(pixbuf, W, H, cx, cy, CHAR_W - 1, CHAR_H - 1, 10);
    }

    win_write(wid, pixbuf, sizeof(pixbuf));
}

static unsigned char term_waitkey(void) {
    unsigned char k;
    while (!(k = (unsigned char)win_getkey(wid))) {
        term_redraw();
        yield();
    }
    return k;
}

// ---- Shell readline ----

static int readline(char *buf, int max) {
    int pos = 0;
    while (1) {
        unsigned char key = term_waitkey();
        if (key == '\n') {
            term_putchar('\n');
            term_redraw();
            break;
        }
        if (key == '\b') {
            if (pos > 0) {
                pos--;
                term_putchar('\b');
                term_redraw();
            }
            continue;
        }
        if (key >= 32 && key < 127 && pos < max - 1) {
            buf[pos++] = (char)key;
            term_putchar((char)key);
            term_redraw();
        }
    }
    buf[pos] = '\0';
    return pos;
}

// ---- Shell builtins ----

static void cmd_help(void) {
    term_print("Built-in commands:\n");
    term_print("  help     - Show this help\n");
    term_print("  ls       - List files in ramfs\n");
    term_print("  echo     - Print arguments\n");
    term_print("  clear    - Clear screen\n");
    term_print("  tasks    - Show PID\n");
    term_print("  shutdown - Power off\n");
    term_print("  exit     - Exit terminal\n");
    term_print("Run any file by name (e.g. hello.elf)\n");
}

static void cmd_ls(void) {
    char name[32];
    unsigned int i = 0;
    while (readdir(i, name, sizeof(name)) > 0) {
        term_print("  ");
        term_print(name);
        term_print("\n");
        i++;
    }
}

static void cmd_echo(const char *line) {
    if (line[4] == ' ') {
        term_print(line + 5);
    }
    term_print("\n");
}

static void cmd_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            screen[r][c] = ' ';
        }
    }
    cur_row = 0;
    cur_col = 0;
}

// ---- Main ----

void _start(void) {
    // Initialize screen buffer
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            screen[r][c] = ' ';
        }
    }

    // Create window
    wid = win_create(W, H, "Term");
    if (wid < 0) {
        exit(1);
    }

    term_print("mateOS terminal\n");
    term_print("Type 'help' for commands.\n\n");
    term_redraw();

    char line[128];

    while (1) {
        term_print("$ ");
        term_redraw();
        int len = readline(line, sizeof(line));

        if (len == 0) continue;

        if (my_strcmp(line, "help") == 0) {
            cmd_help();
        } else if (my_strcmp(line, "ls") == 0) {
            cmd_ls();
        } else if (my_strcmp(line, "exit") == 0) {
            term_print("Bye!\n");
            term_redraw();
            break;
        } else if (my_strcmp(line, "clear") == 0) {
            cmd_clear();
        } else if (my_strncmp(line, "echo ", 5) == 0 || my_strcmp(line, "echo") == 0) {
            if (len > 4) {
                cmd_echo(line);
            } else {
                term_print("\n");
            }
        } else if (my_strcmp(line, "shutdown") == 0) {
            term_print("Shutting down..\n");
            term_redraw();
            shutdown();
        } else if (my_strcmp(line, "tasks") == 0) {
            taskinfo_entry_t tlist[16];
            int count = tasklist(tlist, 16);
            term_print("PID  State    Name\n");
            term_print("---  -------  ----\n");
            for (int i = 0; i < count; i++) {
                term_print_num((int)tlist[i].id);
                term_print("    ");
                switch (tlist[i].state) {
                    case 0: term_print("ready  "); break;
                    case 1: term_print("run    "); break;
                    case 2: term_print("block  "); break;
                    default: term_print("???    "); break;
                }
                term_print("  ");
                term_print(tlist[i].name);
                term_print("\n");
            }
        } else {
            int child = spawn(line);
            if (child >= 0) {
                term_print("[run ");
                term_print(line);
                term_print("]\n");
                term_redraw();
                // Non-blocking wait: keep rendering while child runs
                int code;
                while ((code = wait_nb(child)) == -1) {
                    term_redraw();
                    yield();
                }
                if (code != 0) {
                    term_print("[exit ");
                    term_print_num(code);
                    term_print("]\n");
                } else {
                    term_print("[done]\n");
                }
            } else {
                term_print("Unknown: ");
                term_print(line);
                term_print("\n");
            }
        }
        term_redraw();
    }

    win_destroy(wid);
    exit(0);
}
