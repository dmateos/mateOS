#include "cmd_shared.h"
#include "libc.h"
#include "syscalls.h"

// Wait for a keypress (blocking via yield loop)
static unsigned char waitkey(void) {
    unsigned char k;
    while (!(k = getkey(0))) {
        yield();
    }
    return k;
}

// ---- Command history ----
#define HIST_MAX 16
#define LINE_MAX 128

static char history[HIST_MAX][LINE_MAX];
static int hist_count = 0; // total entries stored (capped at HIST_MAX)
static int hist_head  = 0; // index of next slot to write (ring)

// Store a non-empty command in the ring buffer
static void hist_push(const char *line) {
    int i;
    for (i = 0; line[i] && i < LINE_MAX - 1; i++)
        history[hist_head][i] = line[i];
    history[hist_head][i] = '\0';
    hist_head = (hist_head + 1) % HIST_MAX;
    if (hist_count < HIST_MAX)
        hist_count++;
}

// Get history entry: offset 1 = most recent, 2 = one before, etc.
// Returns NULL if out of range
static const char *hist_get(int offset) {
    if (offset < 1 || offset > hist_count)
        return (void *)0;
    int idx = (hist_head - offset + HIST_MAX * 2) % HIST_MAX;
    return history[idx];
}

// ---- Tab completion ----
// Complete the token at the start of buf against /bin/*.elf and /bin/*.wlf
// On unique match: fills buf, returns completed length.
// On ambiguous/no match: prints candidates, returns -1 (buf unchanged).
static int tab_complete(char *buf, int pos, int max) {
    // Only complete first token and only if no space yet
    int i;
    for (i = 0; i < pos; i++)
        if (buf[i] == ' ')
            return -1;

    // Collect matches from /bin/
    char matches[32][32];
    int match_count = 0;
    char entry[64];

    for (int idx = 0; ; idx++) {
        if (readdir_path("/bin", (unsigned int)idx, entry) <= 0)
            break;
        // entry is just the filename, e.g. "ls.elf"
        // Strip extension to get the command name for comparison
        int elen = strlen(entry);
        // Only match .elf and .wlf
        if (elen < 5)
            continue;
        if (entry[elen-4] != '.')
            continue;
        char *ext = entry + elen - 3;
        int is_elf = (ext[0]=='e' && ext[1]=='l' && ext[2]=='f');
        int is_wlf = (ext[0]=='w' && ext[1]=='l' && ext[2]=='f');
        if (!is_elf && !is_wlf)
            continue;

        // Command name without extension
        char cmd[32];
        int clen = elen - 4; // strip ".elf" or ".wlf"
        if (clen <= 0 || clen >= (int)sizeof(cmd))
            continue;
        for (int j = 0; j < clen; j++)
            cmd[j] = entry[j];
        cmd[clen] = '\0';

        // Check if cmd starts with current buf contents
        if (pos > clen)
            continue;
        int match = 1;
        for (int j = 0; j < pos; j++) {
            if (buf[j] != cmd[j]) { match = 0; break; }
        }
        if (!match)
            continue;

        // Deduplicate: skip if same cmd name already in matches (elf+wlf)
        int dup = 0;
        for (int j = 0; j < match_count; j++) {
            if (strcmp(matches[j], cmd) == 0) { dup = 1; break; }
        }
        if (!dup && match_count < 32) {
            for (int j = 0; j < clen && j < 31; j++)
                matches[match_count][j] = cmd[j];
            matches[match_count][clen < 31 ? clen : 31] = '\0';
            match_count++;
        }
    }

    if (match_count == 0)
        return -1;

    if (match_count == 1) {
        // Unique match — fill buf
        int clen = strlen(matches[0]);
        if (clen >= max - 1)
            clen = max - 2;
        for (i = 0; i < clen; i++)
            buf[i] = matches[0][i];
        buf[clen] = '\0';
        return clen;
    }

    // Ambiguous — show candidates on new line
    print("\n");
    for (int j = 0; j < match_count; j++) {
        print(matches[j]);
        print("  ");
    }
    print("\n");
    return -1;
}

// Read a line with history (up/down) and tab completion
static int readline(char *buf, int max) {
    int pos = 0;  // current cursor position within buf
    int len = 0;  // total length of buf
    int hist_off = 0; // 0 = current input, 1+ = scrolling back
    char saved[LINE_MAX]; // saves current input while browsing history
    saved[0] = '\0';
    buf[0] = '\0';

    while (1) {
        unsigned char key = waitkey();

        if (key == '\n') {
            print_char('\n');
            buf[len] = '\0';
            break;
        }

        if (key == '\t') {
            // Tab completion — only makes sense at end of first word
            char tmp[LINE_MAX];
            for (int i = 0; i < len; i++) tmp[i] = buf[i];
            tmp[len] = '\0';
            int newlen = tab_complete(tmp, len, max);
            if (newlen > 0 && newlen > len) {
                // Erase current input on screen
                for (int i = 0; i < len; i++)
                    print("\b \b");
                for (int i = 0; i < newlen; i++)
                    buf[i] = tmp[i];
                buf[newlen] = '\0';
                len = pos = newlen;
                print(buf);
            } else if (newlen < 0 && pos < len) {
                // Candidates were printed — reprint prompt+buf
                // (reprint happens at top of next prompt iteration)
            }
            continue;
        }

        if (key == '\b') {
            if (pos > 0) {
                // Delete char before cursor: shift tail left
                for (int i = pos - 1; i < len - 1; i++)
                    buf[i] = buf[i + 1];
                len--;
                pos--;
                buf[len] = '\0';
                // Redraw from cursor to end
                print("\b");
                for (int i = pos; i < len; i++)
                    print_char(buf[i]);
                print(" \b"); // erase last char
                // Move cursor back to pos
                for (int i = pos; i < len; i++)
                    print("\b");
            }
            continue;
        }

        if (key == KEY_LEFT) {
            if (pos > 0) {
                pos--;
                print("\b");
            }
            continue;
        }

        if (key == KEY_RIGHT) {
            if (pos < len) {
                print_char(buf[pos]);
                pos++;
            }
            continue;
        }

        if (key == KEY_UP) {
            if (hist_off == 0) {
                // Save current input before browsing
                for (int i = 0; i < len; i++) saved[i] = buf[i];
                saved[len] = '\0';
            }
            int next = hist_off + 1;
            const char *h = hist_get(next);
            if (!h) continue;
            hist_off = next;
            // Erase current line
            for (int i = 0; i < pos; i++) print("\b");
            for (int i = 0; i < len; i++) print(" ");
            for (int i = 0; i < len; i++) print("\b");
            // Replace with history entry
            int hlen = strlen(h);
            if (hlen >= max) hlen = max - 1;
            for (int i = 0; i < hlen; i++) buf[i] = h[i];
            buf[hlen] = '\0';
            len = pos = hlen;
            print(buf);
            continue;
        }

        if (key == KEY_DOWN) {
            if (hist_off == 0) continue;
            hist_off--;
            // Erase current line
            for (int i = 0; i < pos; i++) print("\b");
            for (int i = 0; i < len; i++) print(" ");
            for (int i = 0; i < len; i++) print("\b");
            if (hist_off == 0) {
                // Restore saved input
                int slen = strlen(saved);
                for (int i = 0; i < slen; i++) buf[i] = saved[i];
                buf[slen] = '\0';
                len = pos = slen;
            } else {
                const char *h = hist_get(hist_off);
                int hlen = h ? strlen(h) : 0;
                if (hlen >= max) hlen = max - 1;
                if (h) for (int i = 0; i < hlen; i++) buf[i] = h[i];
                buf[hlen] = '\0';
                len = pos = hlen;
            }
            print(buf);
            continue;
        }

        // Printable character: insert at cursor
        if (key >= 32 && key < 127 && len < max - 1) {
            // Shift tail right
            for (int i = len; i > pos; i--)
                buf[i] = buf[i - 1];
            buf[pos] = (char)key;
            len++;
            buf[len] = '\0';
            // Print from pos to end
            for (int i = pos; i < len; i++)
                print_char(buf[i]);
            pos++;
            // Move cursor back to pos
            for (int i = pos; i < len; i++)
                print("\b");
        }
    }
    buf[len] = '\0';
    return len;
}

static void cmd_clear(void) {
    for (int i = 0; i < 25; i++) {
        print("\n");
    }
}

// ---- Background jobs tracking ----
#define MAX_BGJOBS 8

static struct {
    int pid;
    char name[32];
} bg_jobs[MAX_BGJOBS];
static int bg_count = 0;

static void bg_add(int pid, const char *name) {
    if (bg_count >= MAX_BGJOBS)
        return;
    bg_jobs[bg_count].pid = pid;
    int i;
    for (i = 0; name[i] && i < 31; i++)
        bg_jobs[bg_count].name[i] = name[i];
    bg_jobs[bg_count].name[i] = '\0';
    bg_count++;
}

// Check for finished background jobs and print notifications
static void bg_check(void) {
    int i = 0;
    while (i < bg_count) {
        int code = wait_nb(bg_jobs[i].pid);
        if (code != -1) {
            print("[");
            print_num(bg_jobs[i].pid);
            print("] done  ");
            print(bg_jobs[i].name);
            if (code != 0) {
                print("  (exit ");
                print_num(code);
                print(")");
            }
            print("\n");
            for (int j = i; j < bg_count - 1; j++)
                bg_jobs[j] = bg_jobs[j + 1];
            bg_count--;
        } else {
            i++;
        }
    }
}

static void cmd_jobs(void) {
    if (bg_count == 0) {
        print("No background jobs\n");
        return;
    }
    for (int i = 0; i < bg_count; i++) {
        print("[");
        print_num(bg_jobs[i].pid);
        print("] running  ");
        print(bg_jobs[i].name);
        print("\n");
    }
}

// Parse a command line in-place into argv tokens (split on spaces).
// Returns argc. Modifies line by inserting NULs.
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

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print("mateOS shell v0.1\n");
    print("Type 'help' for commands.\n\n");

    char line[LINE_MAX];
    cmd_io_t io = {.print = print,
                   .print_num = print_num,
                   .clear = cmd_clear,
                   .exit_help = "Exit shell"};

    while (1) {
        bg_check();

        // Show cwd in prompt
        {
            char cwdbuf[64];
            if (getcwd(cwdbuf, sizeof(cwdbuf))) {
                print(cwdbuf);
            }
            print("$ ");
        }
        int len = readline(line, sizeof(line));

        if (len == 0)
            continue;

        hist_push(line);

        // 'jobs' builtin
        if (len == 4 && line[0]=='j' && line[1]=='o' && line[2]=='b' && line[3]=='s') {
            bg_check();
            cmd_jobs();
            continue;
        }

        // cd builtin
        if (len >= 2 && line[0]=='c' && line[1]=='d' &&
            (line[2]==' ' || line[2]=='\0')) {
            const char *dir = (line[2]==' ') ? line + 3 : "/";
            while (*dir == ' ') dir++;
            if (*dir == '\0') dir = "/";
            if (chdir(dir) < 0) {
                print("cd: no such directory: ");
                print(dir);
                print("\n");
            }
            continue;
        }

        // pwd builtin
        if (len == 3 && line[0]=='p' && line[1]=='w' && line[2]=='d') {
            char cwdbuf[64];
            if (getcwd(cwdbuf, sizeof(cwdbuf))) {
                print(cwdbuf);
                print("\n");
            }
            continue;
        }

        cmd_result_t builtin = cmd_try_builtin(line, &io);
        if (builtin == CMD_HANDLED) continue;
        if (builtin == CMD_EXIT) {
            print("Goodbye!\n");
            exit(0);
        }

        // Check for trailing '&' (background mode)
        int background = 0;
        if (len > 0 && line[len - 1] == '&') {
            background = 1;
            len--;
            line[len] = '\0';
            while (len > 0 && line[len - 1] == ' ') {
                len--;
                line[len] = '\0';
            }
        }

        if (len == 0)
            continue;

        // Parse command line into argv tokens
        const char *args[16];
        int ac = parse_argv(line, args, 16);
        if (ac == 0)
            continue;

        // Resolve command: try bin/<cmd>.elf then bin/<cmd>.wlf
        char progname[64];
        const char *cmd = args[0];
        int cmdlen = strlen(cmd);
        int has_ext = (cmdlen >= 4 && cmd[cmdlen-4] == '.' &&
                       ((cmd[cmdlen-3]=='e' && cmd[cmdlen-2]=='l' && cmd[cmdlen-1]=='f') ||
                        (cmd[cmdlen-3]=='w' && cmd[cmdlen-2]=='l' && cmd[cmdlen-1]=='f')));
        if (!has_ext) {
            progname[0]='b'; progname[1]='i'; progname[2]='n'; progname[3]='/';
            int i;
            for (i = 0; i < 55 && cmd[i]; i++)
                progname[4+i] = cmd[i];
            progname[4+i]='.'; progname[5+i]='e'; progname[6+i]='l';
            progname[7+i]='f'; progname[8+i]='\0';
            args[0] = progname;
        }

        int child = spawn_argv(args[0], args, ac);
        if (child < 0 && !has_ext) {
            int i;
            progname[0]='b'; progname[1]='i'; progname[2]='n'; progname[3]='/';
            for (i = 0; i < 55 && cmd[i]; i++)
                progname[4+i] = cmd[i];
            progname[4+i]='.'; progname[5+i]='w'; progname[6+i]='l';
            progname[7+i]='f'; progname[8+i]='\0';
            args[0] = progname;
            child = spawn_argv(args[0], args, ac);
        }
        if (child >= 0) {
            if (background) {
                print("[");
                print_num(child);
                print("] ");
                print(args[0]);
                print("\n");
                bg_add(child, args[0]);
            } else {
                int code = wait(child);
                if (code != 0) {
                    print("[exited with code ");
                    print_num(code);
                    print("]\n");
                }
            }
        } else {
            print("Unknown command: ");
            print(cmd);
            print("\n");
        }
    }
}
