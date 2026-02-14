// Window terminal - shell running inside a WM window
// Renders text to pixel buffer, routes I/O through window syscalls

#include "ugfx.h"
#include "syscalls.h"
#include "cmd_shared.h"

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

// Parse a command line in-place into argv tokens (split on spaces).
static int parse_argv(char *line, const char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

void _start(int argc_unused, char **argv_unused) {
    (void)argc_unused; (void)argv_unused;
    // Initialize screen buffer
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            screen[r][c] = ' ';
        }
    }

    // Create window
    wid = win_create(W, H, "Term");
    if (wid < 0) {
        // Can't print to window, use kernel console
        write(1, "error: requires window manager\n", 31);
        exit(1);
    }

    // Redirect stdout to this window so spawned children's output appears here
    win_set_stdout(wid);

    term_print("mateOS terminal\n");
    term_print("Type 'help' for commands.\n\n");
    term_redraw();

    char line[128];
    cmd_io_t io = {
        .print = term_print,
        .print_num = term_print_num,
        .clear = cmd_clear,
        .exit_help = "Exit terminal"
    };

    while (1) {
        term_print("$ ");
        term_redraw();
        int len = readline(line, sizeof(line));

        if (len == 0) continue;

        cmd_result_t builtin = cmd_try_builtin(line, &io);
        if (builtin == CMD_HANDLED) {
            term_redraw();
            continue;
        }
        if (builtin == CMD_EXIT) {
            term_print("Bye!\n");
            term_redraw();
            break;
        }

        const char *args[16];
        int ac = parse_argv(line, args, 16);
        if (ac == 0) continue;

        // Auto-append .elf if not already present
        char elfname[64];
        const char *cmd = args[0];
        int cmdlen = 0;
        while (cmd[cmdlen]) cmdlen++;
        if (cmdlen < 4 || cmd[cmdlen-4] != '.' || cmd[cmdlen-3] != 'e' ||
            cmd[cmdlen-2] != 'l' || cmd[cmdlen-1] != 'f') {
            int i;
            for (i = 0; i < 59 && cmd[i]; i++) elfname[i] = cmd[i];
            elfname[i++] = '.'; elfname[i++] = 'e';
            elfname[i++] = 'l'; elfname[i++] = 'f'; elfname[i] = '\0';
            args[0] = elfname;
        }

        int child = spawn_argv(args[0], args, ac);
        if (child >= 0) {
            term_print("[run ");
            term_print(args[0]);
            term_print("]\n");
            term_redraw();
            // Non-blocking wait: keep rendering while child runs
            // Drain child's stdout text from window text buffer
            int code;
            char tbuf[256];
            while ((code = wait_nb(child)) == -1) {
                int n = win_read_text(wid, tbuf, sizeof(tbuf) - 1);
                if (n > 0) {
                    tbuf[n] = '\0';
                    term_print(tbuf);
                }
                term_redraw();
                yield();
            }
            // Drain any remaining text after child exits
            {
                int n;
                while ((n = win_read_text(wid, tbuf, sizeof(tbuf) - 1)) > 0) {
                    tbuf[n] = '\0';
                    term_print(tbuf);
                }
            }
            if (code == -3) {
                term_print("[detached]\n");
            } else if (code != 0) {
                term_print("[exit ");
                term_print_num(code);
                term_print("]\n");
            } else {
                term_print("[done]\n");
            }
        } else {
            term_print("Unknown: ");
            term_print(args[0]);
            term_print("\n");
        }
        term_redraw();
    }

    win_set_stdout(-1);
    win_destroy(wid);
    exit(0);
}
