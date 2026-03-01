#include "libc.h"
#include "syscalls.h"
#include "ugfx.h"

#define W 500
#define H 350
#define MAX_TASKS_VIEW 32
#define TITLE_H 0
#define STATS_H 36
#define STATUS_H 14
#define ROW_H 10

#define COL_BG 237
#define COL_TITLE 75
#define COL_TITLE_2 117
#define COL_TITLE_TXT 255
#define COL_HDR_BG 239
#define COL_HDR_TXT 252
#define COL_ROW_A 238
#define COL_ROW_B 240
#define COL_ROW_TXT 251
#define COL_SEL_BG 31
#define COL_SEL_TXT 255
#define COL_RUN_TXT 120
#define COL_STATUS 236
#define COL_STATUS_TXT 250
#define COL_CPU_BAR_BG 242
#define COL_CPU_BAR_FG 81
#define COL_GRAPH_BG 235
#define COL_GRAPH_GRID 239
#define COL_GRAPH_LINE 74
#define COL_GRAPH_FILL 31

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
static unsigned char cpu_hist[96];
static int cpu_hist_len = 0;
static int cpu_hist_pos = 0;
static int cpu_total_pct = 0;
static int running_count = 0;
static int mem_used_pct = 0;
static unsigned int pmm_used_frames = 0;
static unsigned int pmm_total_frames = 0;

static void copy_status(const char *s) {
    int i = 0;
    while (s[i] && i < (int)sizeof(status) - 1) {
        status[i] = s[i];
        i++;
    }
    status[i] = '\0';
}

static int parse_u32_after(const char *s, const char *key, unsigned int *out) {
    const char *p = strstr(s, key);
    if (!p)
        return -1;
    p += strlen(key);
    unsigned int v = 0;
    int any = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (unsigned int)(*p - '0');
        p++;
        any = 1;
    }
    if (!any)
        return -1;
    *out = v;
    return 0;
}

static void refresh_mem_stats(void) {
    char mem[256];
    int fd = open("/mos/kmem", O_RDONLY);
    if (fd < 0)
        return;
    int n = fd_read(fd, mem, sizeof(mem) - 1);
    close(fd);
    if (n <= 0)
        return;
    mem[n] = '\0';

    unsigned int total = 0, used = 0;
    if (parse_u32_after(mem, "PMM: total=", &total) == 0 &&
        parse_u32_after(mem, " used=", &used) == 0 && total > 0) {
        pmm_total_frames = total;
        pmm_used_frames = used;
        mem_used_pct = (int)((used * 100u) / total);
        if (mem_used_pct < 0)
            mem_used_pct = 0;
        if (mem_used_pct > 100)
            mem_used_pct = 100;
    }
}

static int state_running(unsigned int st) { return st == 1; }

static const char *state_name(unsigned int st) {
    if (st == 0)
        return "READY";
    if (st == 1)
        return "RUN";
    if (st == 2)
        return "BLK";
    if (st == 3)
        return "TERM";
    return "?";
}

static int sample_find(int pid) {
    for (int i = 0; i < MAX_TASKS_VIEW; i++) {
        if (samples[i].pid == pid)
            return i;
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
    if (si < 0)
        si = sample_alloc((int)t->id);
    if (si < 0)
        return;

    if (delta_total == 0) {
        samples[si].runtime_prev = t->runtime_ticks;
        samples[si].cpu_pct = 0;
        return;
    }

    unsigned int prev = samples[si].runtime_prev;
    unsigned int cur = t->runtime_ticks;
    unsigned int delta_task = (cur >= prev) ? (cur - prev) : 0;
    int pct = (int)((delta_task * 100u) / delta_total);
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;

    samples[si].runtime_prev = cur;
    samples[si].cpu_pct = pct;
}

static int sample_cpu_percent(int pid) {
    int si = sample_find(pid);
    if (si < 0)
        return 0;
    return samples[si].cpu_pct;
}

static void refresh_tasks(void) {
    unsigned int now_ticks = get_ticks();
    unsigned int delta_total = 0;
    if (prev_total_ticks != 0 && now_ticks >= prev_total_ticks) {
        delta_total = now_ticks - prev_total_ticks;
    }

    task_count = tasklist(tasks, MAX_TASKS_VIEW);
    if (task_count < 0)
        task_count = 0;
    if (selected >= task_count)
        selected = (task_count > 0) ? (task_count - 1) : 0;

    for (int i = 0; i < task_count; i++) {
        sample_update(&tasks[i], delta_total);
    }

    cpu_total_pct = 0;
    running_count = 0;
    for (int i = 0; i < task_count; i++) {
        int pct = sample_cpu_percent((int)tasks[i].id);
        if (tasks[i].id != 0)
            cpu_total_pct += pct;
        if (state_running(tasks[i].state))
            running_count++;
    }
    if (cpu_total_pct > 100)
        cpu_total_pct = 100;
    refresh_mem_stats();
    if (cpu_hist_len < (int)sizeof(cpu_hist)) {
        cpu_hist[cpu_hist_len++] = (unsigned char)cpu_total_pct;
    } else {
        cpu_hist[cpu_hist_pos] = (unsigned char)cpu_total_pct;
        cpu_hist_pos++;
        if (cpu_hist_pos >= (int)sizeof(cpu_hist))
            cpu_hist_pos = 0;
    }

    prev_total_ticks = now_ticks;
}

static int visible_rows(void) {
    int rows = (H - TITLE_H - STATS_H - STATUS_H - ROW_H - 8) / ROW_H;
    if (rows < 1)
        rows = 1;
    return rows;
}

static void keep_selection_visible(void) {
    int rows = visible_rows();
    if (selected < view_top)
        view_top = selected;
    if (selected >= view_top + rows)
        view_top = selected - rows + 1;
    if (view_top < 0)
        view_top = 0;
    if (task_count > rows && view_top > task_count - rows)
        view_top = task_count - rows;
    if (task_count <= rows)
        view_top = 0;
}

static const char *ring_name(unsigned int ring) {
    if (ring == 0)
        return "K";
    if (ring == 3)
        return "U";
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

static void draw_cpu_bar(int x, int y, int pct, int selected_row) {
    int w = 20;
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    ugfx_buf_rect(buf, W, H, x, y + 1, w, 6,
                  selected_row ? 233 : COL_CPU_BAR_BG);
    int fill = (pct * (w - 2)) / 100;
    if (fill > 0) {
        ugfx_buf_rect(buf, W, H, x + 1, y + 2, fill, 4,
                      selected_row ? COL_TITLE_2 : COL_CPU_BAR_FG);
    }
}

static void draw_meter(int x, int y, int w, int pct, unsigned char fill_col) {
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    ugfx_buf_rect(buf, W, H, x, y, w, 6, COL_CPU_BAR_BG);
    int fill = (pct * (w - 2)) / 100;
    if (fill > 0)
        ugfx_buf_rect(buf, W, H, x + 1, y + 1, fill, 4, fill_col);
}

static void draw_stats_panel(void) {
    int x = 0;
    int y = TITLE_H;
    int w = W;
    int h = STATS_H;
    ugfx_buf_rect(buf, W, H, x, y, w, h, COL_HDR_BG);
    ugfx_buf_hline(buf, W, H, x, y, w, 242);
    ugfx_buf_hline(buf, W, H, x, y + h - 1, w, 233);

    draw_str(6, y + 4, "USER CPU", COL_HDR_TXT);
    draw_num(70, y + 4, cpu_total_pct, COL_TITLE_TXT);
    draw_str(94, y + 4, "%", COL_HDR_TXT);
    draw_meter(6, y + 16, 96, cpu_total_pct, COL_CPU_BAR_FG);

    draw_str(116, y + 4, "MEM", COL_HDR_TXT);
    draw_num(148, y + 4, mem_used_pct, COL_TITLE_TXT);
    draw_str(172, y + 4, "%", COL_HDR_TXT);
    draw_meter(116, y + 16, 84, mem_used_pct, 180);

    draw_str(210, y + 4, "Tasks", COL_HDR_TXT);
    draw_num(258, y + 4, task_count, COL_TITLE_TXT);
    draw_str(292, y + 4, "Run", COL_HDR_TXT);
    draw_num(324, y + 4, running_count, COL_TITLE_TXT);
    if (pmm_total_frames > 0) {
        draw_str(210, y + 16, "PMM", COL_HDR_TXT);
        draw_num(242, y + 16, (int)pmm_used_frames, COL_ROW_TXT);
        draw_str(274, y + 16, "/", COL_HDR_TXT);
        draw_num(282, y + 16, (int)pmm_total_frames, COL_ROW_TXT);
    }

    int gx = 350;
    int gy = y + 4;
    int gw = W - gx - 8;
    int gh = h - 10;
    ugfx_buf_rect(buf, W, H, gx, gy, gw, gh, COL_GRAPH_BG);
    ugfx_buf_hline(buf, W, H, gx, gy, gw, COL_GRAPH_GRID);
    ugfx_buf_hline(buf, W, H, gx, gy + gh / 2, gw, COL_GRAPH_GRID);
    ugfx_buf_hline(buf, W, H, gx, gy + gh - 1, gw, 233);
    for (int i = 0; i < gw - 2; i++) {
        int idx;
        if (cpu_hist_len <= 0)
            break;
        if (cpu_hist_len < (int)sizeof(cpu_hist)) {
            idx = cpu_hist_len - 1 - i;
            if (idx < 0)
                break;
        } else {
            idx = cpu_hist_pos - 1 - i;
            while (idx < 0)
                idx += (int)sizeof(cpu_hist);
        }
        int pct = (int)cpu_hist[idx];
        if (pct < 0)
            pct = 0;
        if (pct > 100)
            pct = 100;
        int bar_h = (pct * (gh - 2)) / 100;
        int px = gx + gw - 2 - i;
        if (bar_h > 0) {
            ugfx_buf_rect(buf, W, H, px, gy + gh - 1 - bar_h, 1, bar_h,
                          COL_GRAPH_FILL);
            ugfx_buf_pixel(buf, W, H, px, gy + gh - 1 - bar_h, COL_GRAPH_LINE);
        }
    }
}

static void redraw(void) {
    ugfx_buf_clear(buf, W, H, COL_BG);

    draw_stats_panel();

    int hdr_y = TITLE_H + STATS_H;
    ugfx_buf_rect(buf, W, H, 0, hdr_y, W, ROW_H + 2, COL_HDR_BG);
    ugfx_buf_hline(buf, W, H, 0, hdr_y, W, 242);
    ugfx_buf_hline(buf, W, H, 0, hdr_y + ROW_H + 1, W, 233);
    draw_str(6, hdr_y + 2, "PID", COL_HDR_TXT);
    draw_str(44, hdr_y + 2, "PPID", COL_HDR_TXT);
    draw_str(88, hdr_y + 2, "R", COL_HDR_TXT);
    draw_str(108, hdr_y + 2, "STATE", COL_HDR_TXT);
    draw_str(164, hdr_y + 2, "CPU", COL_HDR_TXT);
    draw_str(222, hdr_y + 2, "NAME", COL_HDR_TXT);

    int y0 = hdr_y + ROW_H + 4;
    int rows = visible_rows();

    for (int i = 0; i < rows; i++) {
        int ti = view_top + i;
        if (ti >= task_count)
            break;
        int y = y0 + i * ROW_H;
        int sel = (ti == selected);
        unsigned char tc = COL_ROW_TXT;
        ugfx_buf_rect(buf, W, H, 0, y - 1, W, ROW_H,
                      (i & 1) ? COL_ROW_A : COL_ROW_B);
        if (sel) {
            ugfx_buf_rect(buf, W, H, 0, y - 1, W, ROW_H, COL_SEL_BG);
            ugfx_buf_hline(buf, W, H, 0, y - 1, W, COL_TITLE_2);
            tc = COL_SEL_TXT;
        } else if (state_running(tasks[ti].state)) {
            tc = COL_RUN_TXT;
        }

        draw_num(6, y, (int)tasks[ti].id, tc);
        draw_num(44, y, (int)tasks[ti].parent_id, tc);
        draw_str(88, y, ring_name(tasks[ti].ring), tc);
        draw_str(108, y, state_name(tasks[ti].state), tc);
        int cpu = sample_cpu_percent((int)tasks[ti].id);
        draw_cpu_bar(160, y, cpu, sel);
        draw_num(184, y, cpu, tc);
        draw_str(206, y, "%", tc);
        draw_str(222, y, tasks[ti].name, tc);
    }

    ugfx_buf_rect(buf, W, H, 0, H - STATUS_H, W, STATUS_H, COL_STATUS);
    ugfx_buf_hline(buf, W, H, 0, H - STATUS_H, W, 242);
    draw_str(4, H - STATUS_H + 3, status, COL_STATUS_TXT);

    win_write(wid, buf, sizeof(buf));
}

static void kill_selected(void) {
    if (task_count <= 0 || selected < 0 || selected >= task_count)
        return;

    int pid = (int)tasks[selected].id;
    if (pid == 0 || pid == self_pid) {
        copy_status("Refusing to kill kernel/self");
        return;
    }

    int rc = kill(pid);
    if (rc == 0)
        copy_status("Task killed");
    else
        copy_status("kill() failed");
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

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
            if (k == 'q' || k == 27)
                break;
            if ((k == 'w' || k == 'W' || k == KEY_UP) && selected > 0)
                selected--;
            if ((k == 's' || k == 'S' || k == KEY_DOWN) &&
                selected + 1 < task_count)
                selected++;
            if (k == 'k' || k == 'K')
                kill_selected();
            if (k == 'r' || k == 'R')
                copy_status("Refreshed");
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
