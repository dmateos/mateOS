#include "ugfx.h"
#include "syscalls.h"
#include "libc.h"

#define W 500
#define H 350
#define MAX_TASKS_VIEW 32
#define TITLE_H 16
#define STATUS_H 14
#define ROW_H 10

#define COL_BG        7
#define COL_TITLE     1
#define COL_TITLE_TXT 15
#define COL_HDR_BG    8
#define COL_HDR_TXT   15
#define COL_ROW_TXT   0
#define COL_SEL_BG    9
#define COL_SEL_TXT   15
#define COL_RUN_TXT   2
#define COL_STATUS    6

typedef struct {
    int pid;
    unsigned int runtime_prev;
    int cpu_pct;
} cpu_sample_t;

static unsigned char buf[W * H];
static taskinfo_entry_t tasks[MAX_TASKS_VIEW];
static cpu_sample_t samples[MAX_TASKS_VIEW];
static int task_count = 0;
static int selected = 0;
static int view_top = 0;
static int wid = -1;
static int self_pid = -1;
static unsigned int prev_total_ticks = 0;
static char status[96] = "Up/Down Select  K Kill  R Refresh  Q Quit";

static void copy_status(const char *s) {
    int i = 0;
    while (s[i] && i < (int)sizeof(status) - 1) {
        status[i] = s[i];
        i++;
    }
    status[i] = '\0';
}

static int state_running(unsigned int st) { return st == 1; }

static const char *state_name(unsigned int st) {
    if (st == 0) return "READY";
    if (st == 1) return "RUN";
    if (st == 2) return "BLK";
    if (st == 3) return "TERM";
    return "?";
}

static int sample_find(int pid) {
    for (int i = 0; i < MAX_TASKS_VIEW; i++) {
        if (samples[i].pid == pid) return i;
    }
    return -1;
}

static int sample_alloc(int pid) {
    for (int i = 0; i < MAX_TASKS_VIEW; i++) {
        if (samples[i].pid == 0) {
            samples[i].pid = pid;
            samples[i].runtime_prev = 0;
            samples[i].cpu_pct = 0;
            return i;
        }
    }
    return -1;
}

static void sample_update(const taskinfo_entry_t *t, unsigned int delta_total) {
    int si = sample_find((int)t->id);
    if (si < 0) si = sample_alloc((int)t->id);
    if (si < 0) return;

    if (delta_total == 0) {
        samples[si].runtime_prev = t->runtime_ticks;
        samples[si].cpu_pct = 0;
        return;
    }

    unsigned int prev = samples[si].runtime_prev;
    unsigned int cur = t->runtime_ticks;
    unsigned int delta_task = (cur >= prev) ? (cur - prev) : 0;
    int pct = (int)((delta_task * 100u) / delta_total);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    samples[si].runtime_prev = cur;
    samples[si].cpu_pct = pct;
}

static int sample_cpu_percent(int pid) {
    int si = sample_find(pid);
    if (si < 0) return 0;
    return samples[si].cpu_pct;
}

static void refresh_tasks(void) {
    unsigned int now_ticks = get_ticks();
    unsigned int delta_total = 0;
    if (prev_total_ticks != 0 && now_ticks >= prev_total_ticks) {
        delta_total = now_ticks - prev_total_ticks;
    }

    task_count = tasklist(tasks, MAX_TASKS_VIEW);
    if (task_count < 0) task_count = 0;
    if (selected >= task_count) selected = (task_count > 0) ? (task_count - 1) : 0;

    for (int i = 0; i < task_count; i++) {
        sample_update(&tasks[i], delta_total);
    }

    prev_total_ticks = now_ticks;
}

static int visible_rows(void) {
    int rows = (H - TITLE_H - STATUS_H - ROW_H - 8) / ROW_H;
    if (rows < 1) rows = 1;
    return rows;
}

static void keep_selection_visible(void) {
    int rows = visible_rows();
    if (selected < view_top) view_top = selected;
    if (selected >= view_top + rows) view_top = selected - rows + 1;
    if (view_top < 0) view_top = 0;
    if (task_count > rows && view_top > task_count - rows) view_top = task_count - rows;
    if (task_count <= rows) view_top = 0;
}

static const char *ring_name(unsigned int ring) {
    if (ring == 0) return "K";
    if (ring == 3) return "U";
    return "?";
}

static void draw_str(int x, int y, const char *s, unsigned char c) {
    ugfx_buf_string(buf, W, H, x, y, s, c);
}

static void draw_num(int x, int y, int n, unsigned char c) {
    char t[16];
    itoa(n, t);
    draw_str(x, y, t, c);
}

static void redraw(void) {
    ugfx_buf_clear(buf, W, H, COL_BG);

    ugfx_buf_rect(buf, W, H, 0, 0, W, TITLE_H, COL_TITLE);
    draw_str(6, 4, "Task Manager", COL_TITLE_TXT);

    ugfx_buf_rect(buf, W, H, 0, TITLE_H, W, ROW_H + 2, COL_HDR_BG);
    draw_str(6, TITLE_H + 2, "PID", COL_HDR_TXT);
    draw_str(44, TITLE_H + 2, "PPID", COL_HDR_TXT);
    draw_str(88, TITLE_H + 2, "R", COL_HDR_TXT);
    draw_str(108, TITLE_H + 2, "STATE", COL_HDR_TXT);
    draw_str(166, TITLE_H + 2, "CPU%", COL_HDR_TXT);
    draw_str(214, TITLE_H + 2, "NAME", COL_HDR_TXT);

    int y0 = TITLE_H + ROW_H + 4;
    int rows = visible_rows();

    for (int i = 0; i < rows; i++) {
        int ti = view_top + i;
        if (ti >= task_count) break;
        int y = y0 + i * ROW_H;
        int sel = (ti == selected);
        unsigned char tc = COL_ROW_TXT;
        if (sel) {
            ugfx_buf_rect(buf, W, H, 0, y - 1, W, ROW_H, COL_SEL_BG);
            tc = COL_SEL_TXT;
        } else if (state_running(tasks[ti].state)) {
            tc = COL_RUN_TXT;
        }

        draw_num(6, y, (int)tasks[ti].id, tc);
        draw_num(44, y, (int)tasks[ti].parent_id, tc);
        draw_str(88, y, ring_name(tasks[ti].ring), tc);
        draw_str(108, y, state_name(tasks[ti].state), tc);
        draw_num(166, y, sample_cpu_percent((int)tasks[ti].id), tc);
        draw_str(192, y, "%", tc);
        draw_str(214, y, tasks[ti].name, tc);
    }

    ugfx_buf_rect(buf, W, H, 0, H - STATUS_H, W, STATUS_H, COL_STATUS);
    draw_str(4, H - STATUS_H + 3, status, 0);

    win_write(wid, buf, sizeof(buf));
}

static void kill_selected(void) {
    if (task_count <= 0 || selected < 0 || selected >= task_count) return;

    int pid = (int)tasks[selected].id;
    if (pid == 0 || pid == self_pid) {
        copy_status("Refusing to kill kernel/self");
        return;
    }

    int rc = kill(pid);
    if (rc == 0) copy_status("Task killed");
    else copy_status("kill() failed");
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    wid = win_create(W, H, "Task Manager");
    if (wid < 0) {
        print("error: requires window manager\n");
        exit(1);
    }
    detach();
    self_pid = getpid();

    refresh_tasks();
    redraw();

    int tick = 0;
    while (1) {
        int k = win_getkey(wid);
        if (k > 0) {
            if (k == 'q' || k == 27) break;
            if ((k == 'w' || k == 'W' || k == KEY_UP) && selected > 0) selected--;
            if ((k == 's' || k == 'S' || k == KEY_DOWN) && selected + 1 < task_count) selected++;
            if (k == 'k' || k == 'K') kill_selected();
            if (k == 'r' || k == 'R') copy_status("Refreshed");
            refresh_tasks();
            keep_selection_visible();
            redraw();
        }

        tick++;
        if (tick % 20 == 0) {
            refresh_tasks();
            keep_selection_visible();
            redraw();
        }
        yield();
    }

    win_destroy(wid);
    exit(0);
}
