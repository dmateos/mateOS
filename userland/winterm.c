// Window terminal - shell running inside a WM window
// Renders text to pixel buffer, routes I/O through window syscalls

#include "cmd_shared.h"
#include "syscalls.h"
#include "ugfx.h"

#define W 500
#define H 350

// Text grid: 8x8 font with margins
#define MARGIN_X 4
#define MARGIN_Y 4
#define CHAR_W 8
#define CHAR_H 10                               // 8px + 2px line spacing
#define TERM_COLS ((W - MARGIN_X * 2) / CHAR_W) // 61
#define TERM_ROWS ((H - MARGIN_Y * 2) / CHAR_H) // 34
#define HIST_LINES 1024

static unsigned char pixbuf[W * H];
static char hist[HIST_LINES][TERM_COLS + 1];
static int cur_line = 0, cur_col = 0;
static int hist_count = 1;
static int view_top = 0;
static int follow_tail = 1;
static int wid = -1;

// ---- Terminal rendering ----

static void line_clear(int line) {
    if (line < 0 || line >= HIST_LINES)
        return;
    for (int c = 0; c < TERM_COLS; c++)
        hist[line][c] = ' ';
    hist[line][TERM_COLS] = '\0';
}

static int max_view_top(void) {
    if (hist_count <= TERM_ROWS)
        return 0;
    return hist_count - TERM_ROWS;
}

static void snap_view_to_tail(void) { view_top = max_view_top(); }

static void hist_shift_up(void) {
    for (int r = 1; r < HIST_LINES; r++) {
        for (int c = 0; c <= TERM_COLS; c++) {
            hist[r - 1][c] = hist[r][c];
        }
    }
    line_clear(HIST_LINES - 1);
    if (cur_line > 0)
        cur_line--;
    if (hist_count > 0)
        hist_count--;
    if (view_top > 0)
        view_top--;
}

static void term_newline(void) {
    cur_col = 0;
    cur_line++;
    if (cur_line >= HIST_LINES) {
        cur_line = HIST_LINES - 1;
        hist_shift_up();
    }
    if (cur_line + 1 > hist_count)
        hist_count = cur_line + 1;
    line_clear(cur_line);
    if (follow_tail)
        snap_view_to_tail();
}

static void term_scroll_view(int delta) {
    int top = view_top + delta;
    int max_top = max_view_top();
    if (top < 0)
        top = 0;
    if (top > max_top)
        top = max_top;
    view_top = top;
    follow_tail = (view_top == max_top);
}

static void term_putchar(char ch) {
    if (ch == '\n') {
        term_newline();
        return;
    }
    if (ch == '\b') {
        if (cur_col > 0) {
            cur_col--;
            hist[cur_line][cur_col] = ' ';
        }
        return;
    }
    if (ch < 32 || ch > 126)
        return;

    if (cur_col >= TERM_COLS) {
        term_newline();
    }

    hist[cur_line][cur_col] = ch;
    cur_col++;
    if (follow_tail)
        snap_view_to_tail();
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
        int lr = view_top + r;
        if (lr >= hist_count)
            break;
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = hist[lr][c];
            if (ch > 32 && ch < 127) {
                int px = MARGIN_X + c * CHAR_W;
                int py = MARGIN_Y + r * CHAR_H;
                ugfx_buf_char(pixbuf, W, H, px, py, ch, 10); // Light green
            }
        }
    }

    // Draw cursor (block)
    if (cur_line >= view_top && cur_line < view_top + TERM_ROWS) {
        int cx = MARGIN_X + cur_col * CHAR_W;
        int cy = MARGIN_Y + (cur_line - view_top) * CHAR_H;
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
        if (key == 27) { // Esc: allow WM close button to terminate terminal
            buf[0] = '\0';
            return -1;
        }
        if (key == KEY_UP) {
            term_scroll_view(-1);
            term_redraw();
            continue;
        }
        if (key == KEY_DOWN) {
            term_scroll_view(1);
            term_redraw();
            continue;
        }
        if (key == '\n') {
            follow_tail = 1;
            snap_view_to_tail();
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
    for (int r = 0; r < HIST_LINES; r++)
        line_clear(r);
    cur_line = 0;
    cur_col = 0;
    hist_count = 1;
    view_top = 0;
    follow_tail = 1;
}

// ---- Main ----

// Parse a command line in-place into argv tokens (split on spaces).
static int parse_argv(char *line, const char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    return argc;
}

void _start(int argc_unused, char **argv_unused) {
    (void)argc_unused;
    (void)argv_unused;
    // Initialize screen buffer
    for (int r = 0; r < HIST_LINES; r++)
        line_clear(r);

    // Create window
    wid = win_create(W, H, "Term");
    if (wid < 0) {
        // Can't print to window, use kernel console
        write(1, "error: requires window manager\n", 31);
        exit(1);
    }
    detach();

    // Redirect stdout to this window so spawned children's output appears here
    win_set_stdout(wid);

    term_print("mateOS terminal\n");
    term_print("Type 'help' for commands.\n\n");
    term_redraw();

    char line[128];
    cmd_io_t io = {.print = term_print,
                   .print_num = term_print_num,
                   .clear = cmd_clear,
                   .exit_help = "Exit terminal"};

    while (1) {
        // Show cwd in prompt
        {
            char cwdbuf[64];
            if (getcwd(cwdbuf, sizeof(cwdbuf))) {
                term_print(cwdbuf);
            }
            term_print("$ ");
        }
        term_redraw();
        int len = readline(line, sizeof(line));
        if (len < 0)
            break;

        if (len == 0)
            continue;

        // cd builtin — must be handled in this process (changes its own cwd)
        if (len >= 2 && line[0] == 'c' && line[1] == 'd' &&
            (line[2] == ' ' || line[2] == '\0')) {
            const char *dir = (line[2] == ' ') ? line + 3 : "/";
            while (*dir == ' ')
                dir++;
            if (*dir == '\0')
                dir = "/";
            if (chdir(dir) < 0) {
                term_print("cd: no such directory: ");
                term_print(dir);
                term_print("\n");
            }
            term_redraw();
            continue;
        }

        // pwd builtin
        if (len == 3 && line[0] == 'p' && line[1] == 'w' && line[2] == 'd') {
            char cwdbuf[64];
            if (getcwd(cwdbuf, sizeof(cwdbuf))) {
                term_print(cwdbuf);
                term_print("\n");
            }
            term_redraw();
            continue;
        }

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
        if (ac == 0)
            continue;

        // Auto-append .elf for legacy CLI commands; fall back to .wlf.
        char progname[64];
        const char *cmd = args[0];
        int cmdlen = 0;
        while (cmd[cmdlen])
            cmdlen++;
        int has_ext = (cmdlen >= 4 && cmd[cmdlen - 4] == '.' &&
                       ((cmd[cmdlen - 3] == 'e' && cmd[cmdlen - 2] == 'l' &&
                         cmd[cmdlen - 1] == 'f') ||
                        (cmd[cmdlen - 3] == 'w' && cmd[cmdlen - 2] == 'l' &&
                         cmd[cmdlen - 1] == 'f')));
        if (!has_ext) {
            int i;
            for (i = 0; i < 59 && cmd[i]; i++)
                progname[i] = cmd[i];
            progname[i++] = '.';
            progname[i++] = 'e';
            progname[i++] = 'l';
            progname[i++] = 'f';
            progname[i] = '\0';
            args[0] = progname;
        }

        int child = spawn_argv(args[0], args, ac);
        if (child < 0 && !has_ext) {
            int i;
            for (i = 0; i < 59 && cmd[i]; i++)
                progname[i] = cmd[i];
            progname[i++] = '.';
            progname[i++] = 'w';
            progname[i++] = 'l';
            progname[i++] = 'f';
            progname[i] = '\0';
            args[0] = progname;
            child = spawn_argv(args[0], args, ac);
        }
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
